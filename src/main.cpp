// Vibration monitor for the Waveshare ESP32-S3-Touch-LCD-2 (SKU 29667):
// QMI8658 accelerometer, 2.0" touch screen, TF card logging, battery readout.
// Screens: acceleration, waveform, FFT X/Y/Z, stroke counter, battery, recording.
// Scroll with swipes, the on-screen arrows, or the BOOT button.
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "AppConfig.h"
#include "ImuTask.h"
#include "Keypad.h"
#include "SignalHistory.h"
#include "Spectrum.h"
#include "StrokeCounter.h"
#include "TouchGestures.h"
#include "hal/Battery.h"
#include "hal/BoardPins.h"
#include "hal/Cst816.h"
#include "hal/Display.h"
#include "hal/I2cBus.h"
#include "hal/SdLogger.h"
#include "ui/Screens.h"

namespace {

hal::Display display;
GFXcanvas16 *canvas = nullptr; // 150 KB: allocated at start so a failure can be reported.
hal::Cst816 touch;
hal::BatteryMonitor batteryMonitor;
hal::SdLogger logger;
ImuTask imuTask;
QueueHandle_t imuEvents = nullptr, gestureEvents = nullptr;

// ---- Measurement state (UI task only) --------------------------------------
SignalHistory<ui::kBins> history;
SampleWindow<ui::kFftSize> window;
StrokeCounter strokes;
float spectrum[ui::kSpectrumBins];
Accel latest{}, shown{}, tare{};
bool haveSample = false, tareSet = false, paused = false, zeroed = false;
uint32_t lastSampleMs = 0, binStart = 0, rateStart = 0, sampleCount = 0;
float actualHz = 0;
const char *workerState = "SZUKAM QMI8658";

// ---- Navigation and input ---------------------------------------------------
PageNavigator nav(ui::kPageCount);
bool keypadOpen = false;
TagEntry keypadEntry;
char tag[TagEntry::kMax + 1] = "";
const char *notice = nullptr;
uint32_t noticeUntil = 0;
struct TouchState { bool down; int16_t x, y; };
TouchState touchState{};                 // Written by the touch task, read by the UI.
portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;
bool bootRaw = false, bootStable = false;
uint32_t bootChange = 0;

bool fresh(uint32_t now) { return haveSample && now - lastSampleMs < app::kStaleMs; }
void setNotice(const char *text, uint32_t now) {
  notice = text;
  noticeUntil = now + 1800;
}

void loadTag() {
  Preferences prefs;
  if (prefs.begin("wibro", true)) {
    String s = prefs.getString("tag", "");
    std::strncpy(tag, s.c_str(), TagEntry::kMax);
    prefs.end();
  }
}
void saveTag() {
  Preferences prefs;
  if (prefs.begin("wibro", false)) {
    prefs.putString("tag", tag);
    prefs.end();
  }
}

// ---- Touch task: polls the controller at 100 Hz and turns it into gestures --
void touchTask(void *) {
  GestureDetector detector;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(app::kTouchPollMs));
    TouchSample sample;
    if (!touch.read(sample)) sample = TouchSample{}; // An I2C error counts as "no touch".
    portENTER_CRITICAL(&touchMux);
    touchState = {sample.down, sample.x, sample.y};
    portEXIT_CRITICAL(&touchMux);
    const Gesture g = detector.update(sample, millis());
    if (g) xQueueSend(gestureEvents, &g, 0);
  }
}

// ---- Model for the screens -------------------------------------------------
ui::Model buildModel(uint32_t now) {
  ui::Model m;
  m.nowMs = now;
  m.pageIndex = nav.index();
  m.page = ui::Page(nav.index());
  m.live = fresh(now);
  m.rateHz = actualHz;
  m.sensorStatus = workerState;
  static ImuDiagnostics diag;
  diag = imuTask.diagnostics();
  m.sensorError = diag.error;
  m.i2cScan = diag.i2cScan;
  m.shown = shown;
  m.tared = tareSet;
  m.history = &history;
  m.window = &window;
  m.spectrum = spectrum;
  m.strokes = &strokes;
  m.paused = paused;
  m.zeroed = zeroed;
  m.droppedSamples = imuTask.dropped();
  m.battery = batteryMonitor.reading();
  m.chargeKnown = batteryMonitor.chargeKnown();
  m.log = logger.status();
  std::strncpy(m.tag, tag, sizeof m.tag - 1);
  m.keypadOpen = keypadOpen;
  std::strncpy(m.keypadDigits, keypadEntry.digits(), sizeof m.keypadDigits - 1);
  m.notice = int32_t(noticeUntil - now) > 0 ? notice : nullptr;
  portENTER_CRITICAL(&touchMux);
  const TouchState t = touchState;
  portEXIT_CRITICAL(&touchMux);
  m.pressed = t.down ? hitNavBar(t.x, t.y) : NavButton::None;
  return m;
}

// ---- Actions ---------------------------------------------------------------
void advanceBins(uint32_t now) {
  if (paused) { binStart = now; return; }
  uint32_t count = elapsedBins(now, binStart);
  if (count > ui::kBins) { // After a long gap only the visible window matters.
    history.clear();
    binStart = now - ui::kBins * ui::kBinMs;
    count = ui::kBins;
  }
  while (count--) {
    history.tick();
    binStart += ui::kBinMs;
  }
}

void togglePause(uint32_t now) {
  paused = !paused;
  if (!paused) { history.clear(); window.clear(); binStart = now; }
}

void toggleTare(uint32_t now) {
  if (tareSet) { tareSet = false; setNotice("TARA WYLACZONA", now); }
  else if (fresh(now)) { tare = latest; tareSet = true; setNotice("TARA USTAWIONA", now); }
  else setNotice("BRAK DANYCH", now);
}

void toggleZero(uint32_t now) {
  if (zeroed) { history.zero({}); zeroed = false; setNotice("PELNY ODCZYT", now); }
  else if (fresh(now)) { history.zero(latest); zeroed = true; setNotice("USTAWIONO ZERO", now); }
  else setNotice("BRAK DANYCH DO ZERA", now);
  binStart = now;
}

void toggleRecording(uint32_t now) {
  const LogStatus s = logger.status();
  if (s.state == LogStatus::State::Recording) {
    logger.stop();
    setNotice("NAGRYWANIE ZATRZYMANE", now);
  } else if (s.state != LogStatus::State::Idle) {
    setNotice("BRAK KARTY SD", now);
  } else if (!fresh(now)) {
    setNotice("BRAK DANYCH Z CZUJNIKA", now);
  } else {
    logger.start(tag, imuTask.sampleRateHz(), imuTask.rangeG());
    setNotice("NAGRYWANIE START", now);
  }
}

void openKeypad(uint32_t now) {
  if (logger.recording()) { setNotice("ZATRZYMAJ NAGRYWANIE", now); return; }
  keypadEntry.set(tag);
  keypadOpen = true;
}

void closeKeypad(bool save, uint32_t now) {
  if (save) {
    std::strncpy(tag, keypadEntry.digits(), TagEntry::kMax);
    tag[TagEntry::kMax] = '\0';
    saveTag();
    setNotice("NUMER ZAPISANY", now);
  }
  keypadOpen = false;
}

void goToPage(bool forward) {
  if (keypadOpen) return;
  forward ? nav.next() : nav.prev();
}

void onButton(NavButton b, uint32_t now) {
  const ui::ActionLabels a = ui::actionLabels(buildModel(now));
  switch (b) {
  case NavButton::Prev: goToPage(false); break;
  case NavButton::Next: goToPage(true); break;
  case NavButton::Action2:
    if (keypadOpen) closeKeypad(true, now);
    else if (a.a2Enabled) toggleRecording(now);
    else setNotice("BRAK KARTY SD", now);
    break;
  case NavButton::Action1:
    if (!a.a1Enabled) break;
    if (keypadOpen) { closeKeypad(false, now); break; }
    switch (ui::Page(nav.index())) {
    case ui::Page::Accel: toggleTare(now); break;
    case ui::Page::Wave:
    case ui::Page::FftX:
    case ui::Page::FftY:
    case ui::Page::FftZ: togglePause(now); break;
    case ui::Page::Strokes: strokes.reset(); setNotice("LICZNIK SKASOWANY", now); break;
    case ui::Page::Log: openKeypad(now); break;
    default: break;
    }
    break;
  default: break;
  }
}

void onGesture(const Gesture &g, uint32_t now) {
  const bool inBar = NavBarLayout::inBar(g.y);
  switch (g.type) {
  case GestureType::SwipeLeft:
    if (!inBar) goToPage(true);
    break;
  case GestureType::SwipeRight:
    if (!inBar) goToPage(false);
    break;
  case GestureType::Tap:
    if (inBar) {
      onButton(hitNavBar(g.x, g.y), now);
    } else if (ui::Page(nav.index()) == ui::Page::Log) {
      if (keypadOpen) {
        const Key k = KeypadLayout::hit(g.x, g.y);
        if (k != Key::None && keypadEntry.press(k)) closeKeypad(true, now);
      } else if (Rect{0, 100, 320, 40}.contains(g.x, g.y)) {
        openKeypad(now); // The tag box.
      }
    }
    break;
  case GestureType::LongPress:
    // Long press on PAUZA of the waveform page: zero / full reading.
    if (inBar && hitNavBar(g.x, g.y) == NavButton::Action1 && ui::Page(nav.index()) == ui::Page::Wave && !keypadOpen)
      toggleZero(now);
    break;
  default:
    break;
  }
}

void pollBoot(uint32_t now) {
  const bool pressed = digitalRead(board::kBootButton) == LOW;
  if (pressed != bootRaw) { bootRaw = pressed; bootChange = now; }
  if (pressed != bootStable && now - bootChange >= 30) {
    bootStable = pressed;
    if (!pressed) goToPage(true); // Release of a short press: next page.
  }
}

void applyEvent(const ImuEvent &e) {
  if (e.kind == ImuEvent::Kind::Sample) {
    latest = e.value;
    lastSampleMs = e.ms;
    haveSample = true;
    ++sampleCount;
    workerState = "BRAK DANYCH";
    strokes.setBiasTau(1.0f); // Plain accelerometer: gravity is filtered out.
    strokes.add(e.value, e.us);
    if (!paused) {
      history.add(e.value);
      // A gap in samples would smear the spectrum: start a new window.
      if (window.size() && uint32_t(e.us - window.newestUs()) > 100000) window.clear();
      window.add(e.value, e.us);
    }
  } else {
    history.discardPending();
    strokes.breakSignal();
    haveSample = false;
    workerState = e.kind == ImuEvent::Kind::Connecting ? "LACZENIE QMI8658" : "BRAK QMI8658";
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  pinMode(board::kBootButton, INPUT_PULLUP);
  hal::I2cBus::begin();
  display.begin();
  canvas = new GFXcanvas16(board::kScreenW, board::kScreenH);
  if (!canvas || !canvas->getBuffer()) {
    Serial.println("Brak RAM dla ekranu (PSRAM wylaczony?)");
    for (;;) delay(1000);
  }
  canvas->setTextWrap(false);
  if (!touch.begin()) Serial.println("Touch: CST816D not found on I2C 0x15");
  batteryMonitor.begin();
  loadTag();
  logger.begin();
  imuEvents = xQueueCreate(512, sizeof(ImuEvent));
  gestureEvents = xQueueCreate(8, sizeof(Gesture));
  imuTask.begin(imuEvents, &logger);
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 2, nullptr, 0);
  binStart = rateStart = millis();
  Serial.printf("Wibrometr ready: %.0f Hz, +-%.0f g, tag '%s'\n", app::kSampleRateHz,
                2.0f * float(1 << app::kRange), tag);
}

void loop() {
  uint32_t now = millis();
  ImuEvent e;
  while (xQueueReceive(imuEvents, &e, 0) == pdTRUE) applyEvent(e);
  Gesture g;
  while (xQueueReceive(gestureEvents, &g, 0) == pdTRUE) onGesture(g, now);
  now = millis();
  advanceBins(now);
  batteryMonitor.update(now);
  pollBoot(now);

  static uint32_t batteryLogAt = 0;
  if (logger.recording() && now - batteryLogAt >= app::kBatteryLogMs) {
    batteryLogAt = now;
    logger.pushBattery(micros(), batteryMonitor.reading().millivolts, batteryMonitor.charging());
  }

  if (now - rateStart >= 1000) {
    actualHz = sampleCount * 1000.0f / float(now - rateStart);
    sampleCount = 0;
    rateStart = now;
    const LogStatus s = logger.status();
    Serial.printf("rate=%.1f Hz, drops=%lu, vbat=%.2f V %s, sd=%d\n", actualHz, (unsigned long)imuTask.dropped(),
                  batteryMonitor.reading().millivolts / 1000.0f, batteryMonitor.charging() ? "charging" : "",
                  int(s.state));
  }

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= app::kFrameMs) {
    lastFrame = now;
    // Smooth the readout a little (about 0.15 s) so the digits stay readable.
    if (fresh(now)) {
      const float k = 0.3f;
      const Accel in{latest.x - (tareSet ? tare.x : 0), latest.y - (tareSet ? tare.y : 0),
                     latest.z - (tareSet ? tare.z : 0)};
      shown = {shown.x + k * (in.x - shown.x), shown.y + k * (in.y - shown.y), shown.z + k * (in.z - shown.z)};
    }
    ui::drawScreen(*canvas, buildModel(now));
    display.flush(*canvas);
  }
  delay(1);
}
