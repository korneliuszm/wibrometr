// Suspension vibration recorder for the Waveshare ESP32-S3-Touch-LCD-2 (env
// waveshare_vibration): QMI8658 at 500 Hz, a measurement file on the TF card each
// time the VL53L0X range finder sees an object closer than the trigger distance.
// Pages: live chart and statistics, session peaks, FFT, range finder settings
// (set by touch, kept in NVS). Status on Serial once per second.
//
// Threads: the sensor task (core 0) owns RecorderCore's sensor-thread side and
// the MeasSink calls; the touch task (core 0) detects swipes and publishes the raw
// touch state; the range task (core 1) polls the range finder (always, except
// while USB owns the card) and requests the start; this loop (core 1) does the UI, the settings (RecorderCore's
// atomic setters) and the card hand-over (CardHandover.h).
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "CardHandover.h"
#include "RecorderCore.h"
#include "MeasStats.h"
#include "LiveSpectrum.h"
#include "MeasWriter.h"
#include "PeakLog.h"
#include "SignalHistory.h"
#include "RangeTask.h"
#include "RangeUi.h"
#include "SwipeGuard.h"
#include "TouchCalibration.h"
#include "UsbPageUi.h"
#include "UsbOwnership.h"
#include "UsbSd.h"
#include "Qmi8658Decode.h"
#include "VibConfig.h"
#include "VibImuTask.h"
#include "VibUi.h"
#include "hal/Battery.h"
#include "hal/BoardPins.h"
#include "hal/Cst816.h"
#include "hal/Display.h"
#include "hal/I2cBus.h"

namespace {

constexpr uint32_t kTouchPollMs = 10;
constexpr uint32_t kStaleMs = 1000;  // No sample for this long: sensor shown as lost.

hal::Display display;  // Also starts the SPI bus shared with the SD card.
GFXcanvas16 *canvas = nullptr;  // 150 KB: allocated at start so a failure can be reported.
hal::Cst816 touch;
hal::BatteryMonitor batteryMonitor;
RecorderCore core(TriggerDetector::Config{vib::kSampleRateHz, 1.0f});
RangeTask rangeTask;
MeasWriter writer;
MeasCapture capture;               // Samples of the measurement being written, for the statistics.
CapturingSink sink(writer, capture);
UsbOwnership own;
VibImuTask imuTask;
QueueHandle_t events = nullptr;

// ---- UI state (main loop only) ---------------------------------------------
SignalHistory<vib::kBins> history;
MeasStats lastStats;  // Of the last finished measurement; the UI shows it.
uint32_t statsSeq = 0;
unsigned lastNumber = 0;
VibPeaks peaks;       // One entry per measurement since power-up (second screen).
LiveSpectrum live;    // Spectrum of the last ~2 s of the signal (first screen).
uint32_t liveComputeUs = 0;  // Time of the last recomputation (log).
PageNavigator pages(vib::kPages);
SwipeGuard swipes;    // Touch task only. Rejects phantom swipes of the touch controller.
std::atomic<int> pageSteps{0};  // Swipes not yet applied: +1 next page, -1 previous.
uint32_t binStart = 0, lastSampleMs = 0;
bool haveSample = false;
rangeui::HoldRepeater repeater;
rangeui::Settings settings;       // Range finder settings in use.
rangeui::Settings savedSettings;  // Value in NVS.
void applySettings();

// contactMs / maxSpeed: from SwipeGuard, for judging whether a press is a real finger.
// Touch position correction (see TouchCalibration.h): written by the main loop, read by the touch
// task, both under touchMux. During calibration it is the identity.
TouchCal touchCal, savedCal;
std::atomic<bool> calibActive{false};
struct CalibTap { bool ready = false; float x = 0, y = 0; };  // Mean position of one finished contact.
CalibTap calibTap;
int calibStep = -1;                               // Main loop: target being asked for, -1 = not calibrating.
touchcal::Point calibMeasured[touchcal::kTargets];
uint32_t calibStartMs = 0;

// moved: largest distance (px, per axis) from where the contact started; a finger on a button
// stays within GestureDetector::kTapSlopPx.
struct TouchState { bool down; int16_t x, y; uint32_t contactMs; int maxSpeed; int moved; };
// Taps (finger down and up without moving) found by the touch task, for the buttons: they act on
// the tap, not on the touch, so a swipe that starts on a button does not press it.
struct TapEvent { int16_t x = 0, y = 0; };
TapEvent lastTap;        // Guarded by touchMux.
uint32_t tapSeq = 0;     // Guarded by touchMux; counts taps.
std::atomic<int> currentPage{0};  // pages.index() for the touch task.
TouchState touchState{};  // Written by the touch task, read by the main loop.
portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Touch task: reads the controller at 100 Hz while it signals a touch ----
// The CST816 pulses its INT line while touched. It is read only then (and for a
// short while after): once idle it falls asleep, and a read of the sleeping chip
// held the shared I2C bus for up to ~6 ms (measured), long enough to delay the
// IMU's 1 ms polls past a sample.
constexpr uint32_t kTouchActiveMs = 150;
std::atomic<uint32_t> touchIntMs{0};
void IRAM_ATTR onTouchInt() { touchIntMs.store(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS); }

// Gestures are detected here, at the steady 10 ms of this task: the main loop
// stalls during screen flushes, and late samples made swipes fail.
// A gesture starting on a button of the current page (settings or USB page).
bool startsOnButton(int x, int y) {
  const int page = currentPage.load();
  if (page == vib::kPageRange) return rangeui::hit(x, y).dir != 0;
  if (page == vib::kPageUsb) return usbui::hit(x, y) != usbui::Button::None;
  return false;
}

SwipeGuard::Result detectSwipe(const TouchSample &sample, uint32_t nowMs) {
  static uint16_t rawX = 0, rawY = 0;  // Last raw point of the contact (the release sample has none).
  static uint8_t chip = 0;             // Last gesture code the controller reported in this contact.
  if (sample.down) {
    rawX = sample.rawX;
    rawY = sample.rawY;
  }
  if (sample.chipGesture) chip = sample.chipGesture;
  SwipeGuard::Result r = swipes.update(sample, nowMs);
  const GestureType type = r.gesture.type;
  // A quick swipe that starts on a button is a tap whose point drifted (seen: 75 px in 70 ms on
  // the "+" button); a real swipe from a button needs the usual 100+ ms.
  const char *why = r.rejected ? "ODRZUCONY" : "ok";
  if ((type == GestureType::SwipeLeft || type == GestureType::SwipeRight) && r.contactMs < 100 &&
      startsOnButton(r.detected.x, r.detected.y)) {
    r.gesture = Gesture{};
    why = "ODRZUCONY (przycisk)";
  }
  if (type == GestureType::Tap && SwipeGuard::tapValid(r.contactMs, r.maxSpeed)) {
    portENTER_CRITICAL(&touchMux);
    lastTap = {r.detected.x, r.detected.y};
    ++tapSeq;
    portEXIT_CRITICAL(&touchMux);
  }
  if (r.rejected || r.gesture || why[0] == 'O')
    Serial.printf("gest %s typ %d od (%d,%d) o (%d,%d), %lu ms, max %d px/ms, raw (%u,%u), chip %02X\n", why,
                  int(r.detected.type), r.detected.x, r.detected.y, r.detected.dx, r.detected.dy,
                  (unsigned long)r.contactMs, r.maxSpeed, rawX, rawY, chip);
  if (r.rejected || r.gesture) chip = 0;
  if (r.gesture.type == GestureType::SwipeLeft) pageSteps.fetch_add(1);
  if (r.gesture.type == GestureType::SwipeRight) pageSteps.fetch_sub(1);
  return r;
}

// While calibrating: the mean position of each contact (skipping its first samples, where the
// controller is still settling), published when the finger lifts.
void trackCalibrationTap(const TouchSample &s) {
  static int n = 0, used = 0;
  static float sumX = 0, sumY = 0;
  static bool sawUp = false;  // The contact that pressed the "calibrate" button is not a tap.
  if (!calibActive.load()) {
    n = used = 0;
    sumX = sumY = 0;
    sawUp = false;
    return;
  }
  if (!sawUp) {
    sawUp = !s.down;
    return;
  }
  if (s.down) {
    if (++n > 4) {
      sumX += s.x;
      sumY += s.y;
      ++used;
    }
    return;
  }
  if (n > 0 && used >= 3) {
    portENTER_CRITICAL(&touchMux);
    calibTap = {true, sumX / used, sumY / used};
    portEXIT_CRITICAL(&touchMux);
  }
  n = used = 0;
  sumX = sumY = 0;
}

std::atomic<uint32_t> touchQuietReads{0};  // Contact samples read while INT was quiet (log).
std::atomic<uint32_t> touchReadErrors{0};  // Failed reads of the controller during a contact (log).
std::atomic<uint32_t> touchGaps{0};        // Short dropouts inside a contact, bridged (log).

void touchTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  bool wasDown = false;
  int16_t lastX = -1, lastY = -1;
  int frozen = 0;
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kTouchPollMs));
    const uint32_t nowMs = xTaskGetTickCount() * portTICK_PERIOD_MS;
    const bool active = digitalRead(board::kTouchInt) == LOW || nowMs - touchIntMs.load() < kTouchActiveMs;
    // Nothing touched: leave the bus to the IMU. While a gesture is still open it
    // gets its "no contact" samples without a read, so it ends.
    if (!active && !wasDown) {
      if (swipes.touching()) detectSwipe(TouchSample{}, nowMs);
      continue;
    }
    TouchSample sample;
    static TouchSample lastRead;
    static int readErrors = 0, gap = 0;
    if (touch.read(sample)) {
      readErrors = 0;
    } else if (lastRead.down && ++readErrors <= 3) {
      // A failed read (the bus is shared with the IMU and the range finder) in the middle of a
      // contact: keep the last point instead of ending the gesture.
      sample = lastRead;
      touchReadErrors.fetch_add(1);
    } else {
      sample = TouchSample{};
    }
    // INT quiet while the controller still reports a touch. During a fast swipe the CST816 does
    // not always pulse INT, and ending the contact here cut swipes into short taps (seen in the
    // log: "30 ms, 14 px", then nothing). A moving point is a finger; the same point repeated
    // with INT quiet is a stale report of a finger that is gone.
    if (!active && sample.down) {
      frozen = (sample.x == lastX && sample.y == lastY) ? frozen + 1 : 0;
      if (frozen >= 3) sample = TouchSample{};
      else touchQuietReads.fetch_add(1);
    } else {
      frozen = 0;
    }
    if (sample.down) {
      if (gap > 0 && swipes.touching()) touchGaps.fetch_add(1);
      gap = 0;
    } else if (swipes.touching()) {
      ++gap;
    }
    lastRead = sample;
    lastX = sample.down ? sample.x : -1;
    lastY = sample.down ? sample.y : -1;
    wasDown = sample.down;
    trackCalibrationTap(sample);  // Raw position, before the correction.
    if (sample.down) {
      portENTER_CRITICAL(&touchMux);
      const TouchCal cal = touchCal;
      portEXIT_CRITICAL(&touchMux);
      sample.x = int16_t(constrain(lroundf(cal.x(sample.x)), 0, board::kScreenW - 1));
      sample.y = int16_t(constrain(lroundf(cal.y(sample.y)), 0, board::kScreenH - 1));
    }
    static int16_t startX = 0, startY = 0;
    static int moved = 0;
    const bool wasTouching = swipes.touching();
    const SwipeGuard::Result r = detectSwipe(sample, nowMs);
    if (sample.down) {
      if (!wasTouching) {
        startX = sample.x;
        startY = sample.y;
        moved = 0;
      }
      moved = std::max(moved, std::max(std::abs(sample.x - startX), std::abs(sample.y - startY)));
    }
    portENTER_CRITICAL(&touchMux);
    touchState = {sample.down, sample.x, sample.y, r.contactMs, r.maxSpeed, moved};
    portEXIT_CRITICAL(&touchMux);
  }
}

TouchState currentTouch() {
  portENTER_CRITICAL(&touchMux);
  const TouchState t = touchState;
  portEXIT_CRITICAL(&touchMux);
  return t;
}

// A finger on a button: down, long enough and without jumps (phantom touches fail this).
bool realPress(const TouchState &t) { return t.down && SwipeGuard::pressValid(t.contactMs, t.maxSpeed); }

// A finger resting on one place (for the "pressed" look of a button and for press-and-hold).
bool stillPress(const TouchState &t) { return realPress(t) && t.moved <= GestureDetector::kTapSlopPx; }

// The next tap not yet handled, if any.
bool takeTap(TapEvent &out) {
  static uint32_t seen = 0;
  portENTER_CRITICAL(&touchMux);
  const uint32_t seq = tapSeq;
  out = lastTap;
  portEXIT_CRITICAL(&touchMux);
  if (seq == seen) return false;
  seen = seq;
  return true;
}

// ---- Touch calibration ---------------------------------------------------------------
TouchCal loadTouchCal() {
  TouchCal c;
  Preferences prefs;
  if (prefs.begin("vib", true)) {
    c = {prefs.getFloat("tcax", 1), prefs.getFloat("tcbx", 0), prefs.getFloat("tcay", 1), prefs.getFloat("tcby", 0)};
    prefs.end();
  }
  return touchcal::plausible(c) ? c : TouchCal{};
}

void saveTouchCal(const TouchCal &c) {
  Preferences prefs;
  if (prefs.begin("vib", false)) {
    prefs.putFloat("tcax", c.ax);
    prefs.putFloat("tcbx", c.bx);
    prefs.putFloat("tcay", c.ay);
    prefs.putFloat("tcby", c.by);
    prefs.end();
  }
}

void setTouchCal(const TouchCal &c) {
  portENTER_CRITICAL(&touchMux);
  touchCal = c;
  portEXIT_CRITICAL(&touchMux);
}

void endCalibration() {
  calibActive = false;
  calibStep = -1;
}

void startCalibration(uint32_t now) {
  if (calibStep >= 0) return;
  setTouchCal(TouchCal{});  // Measure the uncorrected position.
  calibStep = 0;
  calibStartMs = now;
  portENTER_CRITICAL(&touchMux);
  calibTap = {};
  portEXIT_CRITICAL(&touchMux);
  calibActive = true;
  Serial.println("Kalibracja dotyku: dotknij srodka kazdego krzyza (5 punktow)");
}

// Started by the "KALIBRACJA DOTYKU" button of the last page or the character 'k' on the serial
// port ('q' aborts): five crosses on the screen, the correction is fitted from the taps, saved
// and applied at once. Gives up after 2 minutes without a tap.
void serviceCalibration(uint32_t now) {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == 'd') {
      rangeTask.toggleDebug();
    } else if (ch == 'f' || ch == 'n' || ch == 'p') {  // Range finder mode from the serial port: fast / normal / precise.
      settings.mode = ch == 'f' ? 0 : ch == 'n' ? 1 : 2;
      applySettings();
      Serial.printf("Tryb dalmierza: %s\n", rangeui::modeName(settings.mode));
    } else if (ch == 'k') {
      startCalibration(now);
    } else if (ch == 'q' && calibStep >= 0) {
      setTouchCal(savedCal);
      endCalibration();
      Serial.println("Kalibracja przerwana");
    }
  }
  if (calibStep < 0) return;
  if (now - calibStartMs > 120000) {
    setTouchCal(savedCal);
    endCalibration();
    Serial.println("Kalibracja: przekroczono czas, bez zmian");
    return;
  }
  CalibTap tap;
  portENTER_CRITICAL(&touchMux);
  tap = calibTap;
  calibTap.ready = false;
  portEXIT_CRITICAL(&touchMux);
  if (!tap.ready) return;
  calibStartMs = now;  // Time is counted from the last tap.
  const touchcal::Point &target = touchcal::kTarget[calibStep];
  calibMeasured[calibStep] = {tap.x, tap.y};
  Serial.printf("kal %d cel (%.0f,%.0f) zmierzono (%.1f,%.1f) blad (%+.1f,%+.1f)\n", calibStep, target.x, target.y,
                tap.x, tap.y, tap.x - target.x, tap.y - target.y);
  if (++calibStep < int(touchcal::kTargets)) return;
  const TouchCal fitted = touchcal::fit(touchcal::kTarget, calibMeasured, touchcal::kTargets);
  endCalibration();
  if (touchcal::plausible(fitted)) {
    savedCal = fitted;
    setTouchCal(fitted);
    saveTouchCal(fitted);
    Serial.printf("Kalibracja gotowa: x' = %.4f x %+.1f, y' = %.4f y %+.1f\n", fitted.ax, fitted.bx, fitted.ay,
                  fitted.by);
  } else {
    setTouchCal(savedCal);
    Serial.printf("Kalibracja odrzucona (x' = %.3f x %+.1f, y' = %.3f y %+.1f): stuknij dokladnie w krzyze\n",
                  fitted.ax, fitted.bx, fitted.ay, fitted.by);
  }
}

// ---- Range finder settings in NVS ----------------------------------------------
rangeui::Settings loadSettings() {
  rangeui::Settings v;
  Preferences prefs;
  if (prefs.begin("vib", true)) {
    v.distanceMm = prefs.getInt("rdist", v.distanceMm);
    v.recordS = prefs.getInt("rrec", v.recordS);
    v.preRollMs = prefs.getInt("rpre", v.preRollMs);
    v.mode = prefs.getInt("rmode", v.mode);
    prefs.end();
  }
  return rangeui::clamp(v);
}

void saveSettings(const rangeui::Settings &v) {
  Preferences prefs;
  if (prefs.begin("vib", false)) {
    prefs.putInt("rdist", v.distanceMm);
    prefs.putInt("rrec", v.recordS);
    prefs.putInt("rpre", v.preRollMs);
    prefs.putInt("rmode", v.mode);
    prefs.end();
  }
}

void applySettings() {
  core.setRecordUs(uint32_t(settings.recordS) * 1000000u);
  core.setPreRollUs(uint32_t(settings.preRollMs) * 1000u);
  writer.setTimes(settings.preRollMs / 1000.0f, float(settings.recordS));
  rangeTask.setSettings(settings.distanceMm, settings.mode);
}

// The buttons of the settings page step a value right away (press, then auto-repeat
// while held). NVS is written once the finger is lifted and the values differ from
// the stored ones (flash wear), and not while a measurement is being written: a
// flash write stalls both cores for a moment and could cost samples.
rangeui::Press serviceSettings(uint32_t now, const TapEvent *tap) {
  const TouchState t = currentTouch();
  // The buttons exist on the settings page only. A tap steps once; a finger held still on a
  // button for 0.5 s repeats (HoldRepeater, every 100 ms). A swipe presses nothing.
  const bool onPage = !calibActive && pages.index() == vib::kPageRange;
  const rangeui::Press under = onPage && stillPress(t) ? rangeui::hit(t.x, t.y) : rangeui::Press{};
  if (onPage && tap) {
    const rangeui::Press p = rangeui::hit(tap->x, tap->y);
    if (p.dir) {
      settings = rangeui::step(settings, p.param, p.dir);
      applySettings();
    }
  }
  const rangeui::Press held =
      under.dir && t.contactMs >= rangeui::HoldRepeater::kFirstMs ? under : rangeui::Press{};
  if (const int dir = repeater.update(held, now)) {
    settings = rangeui::step(settings, held.param, dir);
    applySettings();
  }
  const rangeui::Press pressed = under;
  const bool changed = settings.distanceMm != savedSettings.distanceMm || settings.recordS != savedSettings.recordS ||
                       settings.preRollMs != savedSettings.preRollMs || settings.mode != savedSettings.mode;
  if (!t.down && changed && core.mode() != RecorderCore::Mode::Recording) {
    saveSettings(settings);
    savedSettings = settings;
  }
  return pressed;
}

// State of the "clear the card" button, for the UI: armed by one press, done by the second.
uint32_t cleanArmedUntil = 0, cleanResultUntil = 0;
int cleanResult = -1;
uint32_t cleanSeqSeen = 0;

bool cleanAllowed() {
  const MeasStatus s = writer.status();
  return core.mode() == RecorderCore::Mode::Armed && !own.hostOwns() && s.state == MeasStatus::State::Idle &&
         s.measurements > 0;
}

// The buttons of the last page, one action per press: USB disk mode (hands the card to the
// computer; refused during a measurement), touch calibration, clearing the card (twice).
usbui::Button serviceUsbPage(uint32_t now, const TapEvent *tap) {
  using B = usbui::Button;
  const TouchState t = currentTouch();
  // The buttons act on a tap (a swipe that starts on one presses nothing); the one under a
  // resting finger is drawn pressed.
  const bool onPage = !calibActive && pages.index() == vib::kPageUsb;
  const B pressed = onPage && stillPress(t) ? usbui::hit(t.x, t.y) : B::None;
  const B tapped = onPage && tap ? usbui::hit(tap->x, tap->y) : B::None;
  if (tapped != B::None) {
    if (tapped == B::Usb) {
      if (usbsd::diskMode()) usbsd::setDiskMode(false);
      else if (usbsd::hostConnected() && core.mode() != RecorderCore::Mode::Recording) usbsd::setDiskMode(true);
    } else if (tapped == B::Calibrate) {
      startCalibration(now);
    } else if (tapped == B::Clean && cleanAllowed()) {
      if (int32_t(now - cleanArmedUntil) < 0 && cleanArmedUntil) {
        cleanArmedUntil = 0;
        writer.requestCleanup();
      } else {
        cleanArmedUntil = now + usbui::kConfirmMs;
        cleanResult = -1;
      }
    }
  }
  // The confirmation lapses on its own, when the page is left, or when the card is not free.
  if (cleanArmedUntil && (int32_t(now - cleanArmedUntil) >= 0 || pages.index() != vib::kPageUsb || !cleanAllowed()))
    cleanArmedUntil = 0;
  if (writer.cleanupSeq() != cleanSeqSeen) {  // A cleanup finished: show its result for a while.
    cleanSeqSeen = writer.cleanupSeq();
    cleanResult = int(writer.cleanedFiles());
    cleanResultUntil = now + usbui::kResultShownMs;
  }
  if (cleanResult >= 0 && int32_t(now - cleanResultUntil) >= 0) cleanResult = -1;
  return pressed;
}

// A horizontal swipe changes the page (wrapping); detected in the touch task.
void servicePages() {
  if (calibActive) {  // Swipes are not pages while calibrating.
    pageSteps.exchange(0);
    return;
  }
  for (int steps = pageSteps.exchange(0); steps != 0; steps += steps > 0 ? -1 : 1)
    steps > 0 ? pages.next() : pages.prev();
  currentPage = pages.index();
}

// ---- Chart history -------------------------------------------------------------
void applyEvent(const VibEvent &e) {
  if (e.kind == VibEvent::Kind::Sample) {
    haveSample = true;
    lastSampleMs = e.ms;
    // In Usb mode the core does not filter, so its dynamic value is frozen: leave
    // the chart empty instead of drawing a flat line.
    if (core.mode() != RecorderCore::Mode::Usb) {
      history.add(e.dynamic);
      live.add(e.dynamic, e.ms * 1000u);  // ms resolution: ~0.05 % on the rate over the ~2 s window.
    } else {
      live.clear();
    }
  } else {  // Connecting / Waiting: no sensor.
    haveSample = false;
    history.discardPending();
    live.clear();
  }
}

bool sensorLive(uint32_t now) { return haveSample && now - lastSampleMs < kStaleMs; }

// Same as advanceBins in src/waveshare/main.cpp.
void advanceBins(uint32_t now) {
  uint32_t count = elapsedBins(now, binStart);
  if (count > vib::kBins) {  // After a long gap only the visible window matters.
    history.clear();
    binStart = now - vib::kBins * vib::kBinMs;
    count = vib::kBins;
  }
  while (count--) {
    history.tick();
    binStart += vib::kBinMs;
  }
}
static_assert(vib::kBinMs == 40, "elapsedBins() counts 40 ms bins");

// Decisions: CardHandover.h.
void handOverCard() {
  static CardHandover handover;
  const CardHandover::Actions a = handover.step(
      own.hostOwns(), core.mode() == RecorderCore::Mode::Usb, [] { return usbsd::release(); });
  if (a.grant) usbsd::grant();
  if (a.usbOn) core.requestUsb(true);
  if (a.park) writer.park();
  if (a.unpark) writer.unpark();
  if (a.usbOff) core.requestUsb(false);
}

const char *stateName(MeasStatus::State s) {
  switch (s) {
  case MeasStatus::State::NoCard: return "NoCard";
  case MeasStatus::State::Idle: return "Idle";
  case MeasStatus::State::Recording: return "Recording";
  case MeasStatus::State::Error: return "Error";
  case MeasStatus::State::Parked: return "Parked";
  case MeasStatus::State::Cleaning: return "Cleaning";
  }
  return "?";
}

} // namespace

void setup() {
  Serial.begin(115200);
  hal::I2cBus::begin();
  display.begin();
  canvas = new GFXcanvas16(board::kScreenW, board::kScreenH);
  if (!canvas || !canvas->getBuffer()) {
    Serial.println("Brak RAM dla ekranu (PSRAM wylaczony?)");
    for (;;) delay(1000);
  }
  canvas->setTextWrap(false);
  // 16384 samples (196 KB) for the statistics of the last measurement: PSRAM; without it
  // the screen just shows no statistics.
  capture.attach(static_cast<Accel *>(heap_caps_malloc(MeasCapture::kCapacity * sizeof(Accel), MALLOC_CAP_SPIRAM)));
  batteryMonitor.begin();
  if (!touch.begin()) Serial.println("Touch: CST816D not found on I2C 0x15");
  pinMode(board::kTouchInt, INPUT_PULLUP);
  attachInterrupt(board::kTouchInt, onTouchInt, FALLING);
  settings = savedSettings = loadSettings();
  savedCal = loadTouchCal();
  setTouchCal(savedCal);
  events = xQueueCreate(1024, sizeof(VibEvent));  // ~2 s of samples for the chart.
  writer.begin(qmi8658::odrHz(qmi8658::odrCode(vib::kSampleRateHz)), qmi8658::rangeG(vib::kRange));
  applySettings();
  rangeTask.begin(&core, settings.distanceMm, settings.mode);  // Before the IMU task: its init holds the bus.
  imuTask.begin(events, &core, &sink);
  usbsd::begin(own, writer);  // Up to ~2 s waiting for the card; always starts USB.
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 2, nullptr, 0);
  binStart = millis();
  Serial.printf("Rejestrator drgan: %.0f Hz, +-%.0f g, wyzwalanie dalmierzem %d mm, %d s\n", vib::kSampleRateHz,
                qmi8658::rangeG(vib::kRange), settings.distanceMm, settings.recordS);
}

void loop() {
  static uint32_t lastPrint = 0, lastFrame = 0, samples = 0;
  static Accel last{};
  static float level = 0;
  VibEvent e;
  float frameMax = 0;
  while (xQueueReceive(events, &e, 0) == pdTRUE) {
    if (e.kind == VibEvent::Kind::Sample) {
      ++samples;
      last = e.dynamic;
      frameMax = fmaxf(frameMax, sqrtf(e.dynamic.x * e.dynamic.x + e.dynamic.y * e.dynamic.y +
                                       e.dynamic.z * e.dynamic.z));
      // Bin by the sample's own time, not by when this loop got it: a screen
      // flush delays the queue by tens of ms, and arrival-time bins came out
      // empty (the chart looked like dots).
      advanceBins(e.ms);
    }
    applyEvent(e);
  }
  const uint32_t now = millis();
  if (!sensorLive(now)) advanceBins(now);  // No samples: keep the chart scrolling.
  // Level meter: follows rises at once, falls back over ~0.3 s.
  level = fmaxf(frameMax, level * 0.85f);
  batteryMonitor.update(now);
  servicePages();
  serviceCalibration(now);
  TapEvent tap;
  const TapEvent *tapped = takeTap(tap) ? &tap : nullptr;
  const rangeui::Press pressed = serviceSettings(now, tapped);
  const usbui::Button usbButton = serviceUsbPage(now, tapped);
  usbsd::service(now);
  if (const float hz = imuTask.measuredHz()) writer.setMeasuredRate(hz);
  handOverCard();
  MeasCapture::View view;
  if (capture.tryAcquire(statsSeq, view)) {  // A measurement has just been closed.
    measstats::computeInto(lastStats, view.data, view.count, view.rateHz);  // In place: ~9 KB.
    statsSeq = view.seq;
    lastNumber = writer.status().number;
    if (lastStats.valid)
      peaks.add({lastNumber, lastStats.magPeak,
                 {lastStats.axis[0].peak, lastStats.axis[1].peak, lastStats.axis[2].peak}});
    capture.release();
  }
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    const MeasStatus s = writer.status();
    const RangeTask::Stats rs = rangeTask.takeStats();
    Serial.printf("vbat %.2f V %.0f%% | rate %.1f Hz, overflows %lu, fft %lu us, dotyk bez INT %lu luki %lu bledy %lu | ", batteryMonitor.reading().millivolts / 1000.0f,
                  batteryMonitor.reading().percent, imuTask.measuredHz(), (unsigned long)imuTask.overflows(),
                  (unsigned long)liveComputeUs, (unsigned long)touchQuietReads.load(),
                  (unsigned long)touchGaps.load(), (unsigned long)touchReadErrors.load());
    Serial.printf("imu %lu/s dyn %.2f %.2f %.2f | dalm %s %u mm [%u..%u, %u odczytow, %u bez echa, %u bledow I2C] | mode %d usb %d | SD %s '%s' M%04u %lu smp %lu B "
                  "%lu ms, %lu pomiarow, %lu MB, drop %lu/%lu, missed %lu, fix %lu\n",
                  (unsigned long)samples, last.x, last.y, last.z, rangeTask.present() ? "ok" : "BRAK",
                  unsigned(rangeTask.distanceMm()), unsigned(rs.minMm), unsigned(rs.maxMm), unsigned(rs.readings),
                  unsigned(rs.noEcho), unsigned(rs.i2cErrors), int(core.mode()),
                  int(own.hostOwns()), stateName(s.state), s.message, s.number, (unsigned long)s.samples,
                  (unsigned long)s.fileBytes, (unsigned long)s.recordingMs, (unsigned long)s.measurements,
                  (unsigned long)s.cardMb, (unsigned long)s.dropped, (unsigned long)imuTask.dropped(),
                  (unsigned long)imuTask.missed(), (unsigned long)s.recoveries);
    samples = 0;
  }
  // Live spectrum about three times a second (3 FFTs of 1024 points; the time is in the log).
  static uint32_t lastLive = 0;
  if (now - lastLive >= 300 && live.ready()) {
    lastLive = now;
    const uint32_t t0 = micros();
    live.compute();
    liveComputeUs = micros() - t0;
  }
  if (now - lastFrame >= vib::kFrameMs) {
    lastFrame = now;
    VibModel m;
    m.mode = core.mode();
    m.sd = writer.status();
    m.range = settings;
    m.rangeState = !rangeTask.present() ? VibModel::RangeState::Missing
                   : rangeTask.ranging() ? VibModel::RangeState::Ranging
                                         : VibModel::RangeState::Paused;
    m.rangeMm = rangeTask.distanceMm();
    m.sensorLive = sensorLive(now);
    m.history = &history;
    m.page = pages.index();
    m.peaks = &peaks;
    m.last = &lastStats;
    m.live = &live;
    m.lastNumber = lastNumber;
    m.hostOwns = own.hostOwns();
    m.nowMs = now;
    m.battery = batteryMonitor.reading();
    m.charging = batteryMonitor.charging();
    m.rangePress = pressed;
    m.calibStep = calibStep;
    m.usbHost = usbsd::hostConnected();
    m.usbDisk = usbsd::diskMode();
    m.usbButton = usbButton;
    m.cleanConfirm = cleanArmedUntil != 0;
    m.cleanResult = cleanResult;
    m.level = level;
    m.missed = imuTask.missed();
    drawVibScreen(*canvas, m);
    display.flush(*canvas);  // In 4 SPI bands, so the card can work in between.
  }
  delay(1);
}
