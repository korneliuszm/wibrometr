// Suspension vibration recorder for the Waveshare ESP32-S3-Touch-LCD-2 (env
// waveshare_vibration): QMI8658 at 500 Hz, a measurement file on the TF card each
// time the vibration crosses the threshold. One screen: state, XYZ chart of the
// dynamic acceleration, threshold set by touch (kept in NVS). Status on Serial
// once per second.
//
// Threads: the sensor task (core 0) owns RecorderCore's sensor-thread side and
// the MeasSink calls; the touch task (core 0) only publishes the raw touch state;
// this loop (core 1) does the UI, the threshold (RecorderCore's atomic setter)
// and the card hand-over (CardHandover.h).
#include <Arduino.h>
#include <Preferences.h>
#include "CardHandover.h"
#include "RecorderCore.h"
#include "MeasWriter.h"
#include "SignalHistory.h"
#include "ThresholdUi.h"
#include "UsbOwnership.h"
#include "UsbSd.h"
#include "Qmi8658Decode.h"
#include "VibConfig.h"
#include "VibImuTask.h"
#include "VibUi.h"
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
RecorderCore core(TriggerDetector::Config{vib::kSampleRateHz, 1.0f, vib::kWarmupUs, vib::kQuietUs},
                  vib::kPreRollUs);
MeasWriter writer;
UsbOwnership own;
VibImuTask imuTask;
QueueHandle_t events = nullptr;

// ---- UI state (main loop only) ---------------------------------------------
SignalHistory<vib::kBins> history;
uint32_t binStart = 0, lastSampleMs = 0;
bool haveSample = false;
thrui::HoldRepeater repeater;
int savedDeci = thrui::kDefaultDeci;  // Value in NVS.

struct TouchState { bool down; int16_t x, y; };
TouchState touchState{};  // Written by the touch task, read by the main loop.
portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Touch task: polls the controller at 100 Hz, publishes the raw state ----
void touchTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kTouchPollMs));
    TouchSample sample;
    if (!touch.read(sample)) sample = TouchSample{};  // An I2C error counts as "no touch".
    portENTER_CRITICAL(&touchMux);
    touchState = {sample.down, sample.x, sample.y};
    portEXIT_CRITICAL(&touchMux);
  }
}

TouchState currentTouch() {
  portENTER_CRITICAL(&touchMux);
  const TouchState t = touchState;
  portEXIT_CRITICAL(&touchMux);
  return t;
}

// ---- Threshold in NVS --------------------------------------------------------
int loadThreshold() {
  Preferences prefs;
  int deci = thrui::kDefaultDeci;
  if (prefs.begin("vib", true)) {
    deci = prefs.getInt("thr", thrui::kDefaultDeci);
    prefs.end();
  }
  return thrui::clampDeci(deci);
}

void saveThreshold(int deci) {
  Preferences prefs;
  if (prefs.begin("vib", false)) {
    prefs.putInt("thr", deci);
    prefs.end();
  }
}

// Buttons step the threshold right away (press, then auto-repeat while held).
// NVS is written once the finger is lifted and the value differs from the stored
// one (flash wear), and not while a measurement is being written: a flash write
// stalls both cores for a moment and could cost samples.
thrui::Button serviceThreshold(uint32_t now) {
  const TouchState t = currentTouch();
  const thrui::Button pressed = t.down ? thrui::hit(t.x, t.y) : thrui::Button::None;
  if (const int dir = repeater.update(pressed, now))
    core.setThresholdDeci(thrui::stepDeci(core.thresholdDeci(), dir));
  const int deci = core.thresholdDeci();
  if (pressed == thrui::Button::None && deci != savedDeci && core.mode() != RecorderCore::Mode::Recording) {
    saveThreshold(deci);
    savedDeci = deci;
  }
  return pressed;
}

// ---- Chart history -------------------------------------------------------------
void applyEvent(const VibEvent &e) {
  if (e.kind == VibEvent::Kind::Sample) {
    haveSample = true;
    lastSampleMs = e.ms;
    // In Usb mode the core does not filter, so its dynamic value is frozen: leave
    // the chart empty instead of drawing a flat line.
    if (core.mode() != RecorderCore::Mode::Usb) history.add(e.dynamic);
  } else {  // Connecting / Waiting: no sensor.
    haveSample = false;
    history.discardPending();
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
  if (!touch.begin()) Serial.println("Touch: CST816D not found on I2C 0x15");
  savedDeci = loadThreshold();
  core.setThresholdDeci(savedDeci);
  events = xQueueCreate(256, sizeof(VibEvent));
  writer.begin(qmi8658::odrHz(qmi8658::odrCode(vib::kSampleRateHz)), qmi8658::rangeG(vib::kRange));
  imuTask.begin(events, &core, &writer);
  usbsd::begin(own, writer);  // Up to ~2 s waiting for the card; always starts USB.
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 2, nullptr, 0);
  binStart = millis();
  Serial.printf("Rejestrator drgan: %.0f Hz, +-%.0f g, prog %.1f m/s2\n", vib::kSampleRateHz,
                qmi8658::rangeG(vib::kRange), core.thresholdDeci() / 10.0f);
}

void loop() {
  static uint32_t lastPrint = 0, lastFrame = 0, samples = 0;
  static Accel last{};
  VibEvent e;
  while (xQueueReceive(events, &e, 0) == pdTRUE) {
    if (e.kind == VibEvent::Kind::Sample) {
      ++samples;
      last = e.dynamic;
    }
    applyEvent(e);
  }
  const uint32_t now = millis();
  advanceBins(now);
  const thrui::Button pressed = serviceThreshold(now);
  usbsd::service(now);
  handOverCard();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    const MeasStatus s = writer.status();
    Serial.printf("imu %lu/s dyn %.2f %.2f %.2f | prog %d | mode %d usb %d | SD %s '%s' M%04u %lu smp %lu B "
                  "%lu ms, %lu pomiarow, %lu MB, drop %lu/%lu\n",
                  (unsigned long)samples, last.x, last.y, last.z, core.thresholdDeci(), int(core.mode()),
                  int(own.hostOwns()), stateName(s.state), s.message, s.number, (unsigned long)s.samples,
                  (unsigned long)s.fileBytes, (unsigned long)s.recordingMs, (unsigned long)s.measurements,
                  (unsigned long)s.cardMb, (unsigned long)s.dropped, (unsigned long)imuTask.dropped());
    samples = 0;
  }
  if (now - lastFrame >= vib::kFrameMs) {
    lastFrame = now;
    VibModel m;
    m.mode = core.mode();
    m.sd = writer.status();
    m.thresholdDeci = core.thresholdDeci();
    m.sensorLive = sensorLive(now);
    m.history = &history;
    m.hostOwns = own.hostOwns();
    m.nowMs = now;
    m.pressed = pressed;
    drawVibScreen(*canvas, m);
    display.flush(*canvas);  // In 4 SPI bands, so the card can work in between.
  }
  delay(1);
}
