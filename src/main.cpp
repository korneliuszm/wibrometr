#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>
#include <atomic>
#include <cmath>
#include <iterator>
#include "SignalHistory.h"
#include "Spectrum.h"
#include "StrokeCounter.h"
#include "Mpu6050.h"
#include "AccelScreen.h"
#include "indoor_positioning/IndoorPositioning.h"
#include "NavigationScreen.h"
#include "UiTheme.h"

namespace {
// MPU6050 accelerometer on I2C (SDA = GPIO42, SCL = GPIO41, the Feather's own
// I2C bus shared with the battery gauge); its data-ready INT is on A5.
constexpr uint8_t mpuIntPin = A5;
constexpr uint32_t i2cHz = 400000;
// Analysis screens fed by the accelerometer: waveform, FFT, stroke counter.
// The plain XYZ readout is always available.
constexpr bool imuEnabled = false;
// Wi-Fi indoor positioning prototype: off (too coarse in practice); the code
// stays, set true to bring back its map screen.
constexpr bool ipsEnabled = false;
constexpr uint32_t binMs = 40, staleMs = 1000, retryMs = 2000;
constexpr int bins = 100;
constexpr std::size_t fftSize = 512; // ~2.6 s at 200 Hz, ~0.4 Hz per bin.
constexpr float fftRange = 5.0f;     // Fixed full scale of the spectrum, m/s2.
constexpr uint32_t fftGapMs = 100, batteryMs = 2000, gaugeRetryMs = 5000;
constexpr float fullScale = 20.0f;
constexpr int plotX = 30, plotY = 28, plotW = 200, plotH = 79;
using ui::bg;
using ui::grid;
using ui::muted;
constexpr uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, 0x3DFF};
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);
SignalHistory<bins> history;
Mpu6050 mpu;
enum Screen { AccelScreen, TimeScreen, FftX, FftY, FftZ, StrokeScreen, NavScreen, BatteryScreen };
// Screens of the enabled features, in BOOT order; battery is always last.
constexpr int countScreens() { return 1 + (imuEnabled ? 5 : 0) + ipsEnabled + 1; }
constexpr int screenCount = countScreens();
struct ScreenList {
  Screen s[screenCount] = {};
  constexpr ScreenList() {
    int n = 0;
    s[n++] = AccelScreen;
    if (imuEnabled)
      for (Screen x : {TimeScreen, FftX, FftY, FftZ, StrokeScreen}) s[n++] = x;
    if (ipsEnabled) s[n++] = NavScreen;
    s[n] = BatteryScreen;
  }
};
constexpr ScreenList activeScreens;
int screenPos = 0; // Index into activeScreens.
Screen screen = activeScreens.s[0];
ips::IndoorPositioning positioning;
Accel tare{}, shown{};     // XYZ screen: offset set by BOOT hold, smoothed readout.
bool tareSet = false;
uint32_t lastNavSignature = 0;
bool navDrawn = false;
SampleWindow<fftSize> window;
StrokeCounter strokes; // Hand strokes in any direction; fed regardless of pause.
float spectrum[fftSize / 2 + 1];
// Feather 5483 carries a MAX17048 (newer) or LC709203F (older) fuel gauge.
enum class Gauge { None, Max17048, Lc709203f };
Gauge gauge = Gauge::None;
Adafruit_LC709203F lc;
uint32_t batteryAt = 0, gaugeProbeAt = 0;
// Published by the sensor task (sole owner of Wire), copied by the display.
struct BatteryInfo { Gauge gauge = Gauge::None; bool valid = false; float volts = 0, percent = 0, rate = 0; };
BatteryInfo battery;
portMUX_TYPE batteryMux = portMUX_INITIALIZER_UNLOCKED;
enum class Kind { Sample, Connecting, Waiting };
struct Event { Kind kind; uint32_t ms; uint32_t us; Accel value; };
QueueHandle_t events;
std::atomic<uint32_t> dropped{0};
bool paused = false, haveSample = false, zeroed = false;
bool buttonRaw = false, buttonStable = false;
uint32_t buttonChange = 0, buttonDown = 0;
uint32_t lastSample = 0, binStart = 0, lastFrame = 0;
uint32_t rateStart = 0, sampleCount = 0;
float actualHz = 0;
Accel latest{};
const char *workerState = "SZUKAM MPU6050";
uint32_t noticeUntil = 0;
const char *notice = "";

void emit(Kind kind, Accel value = {}, uint32_t us = 0) {
  Event event{kind, millis(), us, value};
  if (xQueueSend(events, &event, 0) != pdTRUE) ++dropped;
}

bool readGaugeWord(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(0x36);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(0x36), uint8_t(2)) != 2)
    return false;
  value = uint16_t(Wire.read()) << 8;
  value |= Wire.read();
  return true;
}
// Runs on the sensor task, which alone owns Wire. MAX17048 is read directly so
// its state-of-charge model is not reset on every boot, as its library does.
void pollBattery(uint32_t now) {
  if (gauge == Gauge::None) {
    if (int32_t(now - gaugeProbeAt) < 0) return;
    gaugeProbeAt = now + gaugeRetryMs;
    uint16_t version = 0;
    if (readGaugeWord(0x08, version) && (version & 0xFFF0) == 0x0010)
      gauge = Gauge::Max17048;
    else if (lc.begin(&Wire)) gauge = Gauge::Lc709203f;
    else return;
    Serial.printf("Battery gauge: %s\n", gauge == Gauge::Max17048 ? "MAX17048" : "LC709203F");
    batteryAt = now;
  }
  if (int32_t(now - batteryAt) < 0) return;
  batteryAt = now + batteryMs;
  BatteryInfo next;
  next.gauge = gauge;
  if (gauge == Gauge::Max17048) {
    uint16_t vcell, soc, crate;
    next.valid = readGaugeWord(0x02, vcell) && readGaugeWord(0x04, soc) && readGaugeWord(0x16, crate);
    if (next.valid) {
      next.volts = vcell * 78.125e-6f;
      next.percent = soc / 256.0f;
      next.rate = int16_t(crate) * 0.208f;
    }
  } else {
    next.volts = lc.cellVoltage();
    next.percent = lc.cellPercent();
    next.valid = std::isfinite(next.volts) && next.volts > 0;
  }
  if (!next.valid) { gauge = Gauge::None; gaugeProbeAt = now + gaugeRetryMs; next.gauge = Gauge::None; }
  portENTER_CRITICAL(&batteryMux);
  battery = next;
  portEXIT_CRITICAL(&batteryMux);
}
// Data-ready interrupt: stamps the sample time and wakes the sensor task.
TaskHandle_t sensorHandle = nullptr;
volatile uint32_t intUs = 0;
void IRAM_ATTR onMpuInt() {
  intUs = micros();
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(sensorHandle, &woken);
  portYIELD_FROM_ISR(woken);
}

// The only task that touches Wire (MPU6050 and the battery gauge). It reads one
// sample per data-ready interrupt; if INT never fires (wire missing) it falls
// back to polling every 5 ms so the readout still works.
void sensorTask(void *) {
  bool ready = false, intSeen = false;
  int errors = 0, timeouts = 0;
  uint32_t retryAt = 0;
  for (;;) {
    if (!ready) {
      const uint32_t now = millis();
      if (int32_t(now - retryAt) >= 0) {
        emit(Kind::Connecting);
        Wire.end(); // Re-init also frees a bus left stuck by a glitch.
        Wire.begin(SDA, SCL, i2cHz);
        Wire.setTimeOut(20);
        if (mpu.begin(Wire)) {
          Serial.printf("MPU6050: addr 0x%02X, WHO_AM_I 0x%02X%s, %.0f Hz, INT on A5\n", mpu.address(),
                        mpu.whoAmI(), mpu.whoAmI() == 0x68 ? "" : " (clone?)", Mpu6050::kSampleRateHz);
          // INT is latched: one read clears it, so the first data-ready after
          // attaching is a clean rising edge.
          Accel discard;
          mpu.read(discard);
          ulTaskNotifyTake(pdTRUE, 0); // Drop stale notifications.
          attachInterrupt(mpuIntPin, onMpuInt, RISING);
          ready = true;
          intSeen = false;
          errors = timeouts = 0;
        } else {
          Serial.println("MPU6050: no answer; check SDA=GPIO42, SCL=GPIO41, 3V, GND, address 0x68/0x69");
          emit(Kind::Waiting);
        }
        retryAt = millis() + retryMs;
      }
      pollBattery(millis());
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    const uint32_t waitMs = intSeen ? 100 : timeouts >= 2 ? 5 : 50;
    const bool signalled = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs)) > 0;
    uint32_t us = micros();
    if (signalled) {
      us = intUs;
      if (!intSeen) Serial.println("MPU6050: INT works");
      intSeen = true;
      timeouts = 0;
    } else if (intSeen || ++timeouts == 2) {
      intSeen = false;
      timeouts = 2;
      Serial.println("MPU6050: no INT (check INT -> A5); polling every 5 ms instead");
    }
    Accel a;
    if (mpu.read(a)) {
      errors = 0;
      emit(Kind::Sample, a, us);
    } else if (++errors >= 20) {
      detachInterrupt(mpuIntPin);
      ready = false;
      retryAt = millis() + retryMs;
      Serial.println("MPU6050: I2C read errors; reconnecting");
      emit(Kind::Waiting);
    }
    pollBattery(millis());
  }
}

void advanceTo(uint32_t now) {
  if (paused) { binStart = now; return; }
  // After a long scheduling gap, only the visible window matters.
  uint32_t count = elapsedBins(now, binStart);
  if (count > bins) {
    history.clear();
    binStart = now - bins * binMs;
    count = bins;
  }
  while (count--) {
    history.tick();
    binStart += binMs;
  }
}

bool fresh(uint32_t now) {
  return haveSample && uint32_t(now - lastSample) < staleMs;
}
void nextScreen() {
  screenPos = (screenPos + 1) % screenCount;
  screen = activeScreens.s[screenPos];
  navDrawn = false;
}
void showNotice(const char *text, uint32_t now) {
  notice = text;
  noticeUntil = now + 1600;
}
void pollButton(uint32_t now) {
  bool pressed = digitalRead(0) == LOW;
  if (pressed != buttonRaw) { buttonRaw = pressed; buttonChange = now; }
  if (pressed != buttonStable && uint32_t(now - buttonChange) >= 30) {
    buttonStable = pressed;
    if (pressed) buttonDown = now;
    else {
      uint32_t duration = now - buttonDown;
      if (ipsEnabled && screen == NavScreen) {
        const auto progress = positioning.getCalibrationProgress();
        if (progress.active) {
          // Calibrating: tap measures the current point, hold cancels.
          if (duration >= 1000) {
            positioning.cancelCalibration();
            showNotice("KALIBRACJA ANULOWANA", now);
          } else if (!progress.measuring) {
            positioning.confirmCalibrationPoint();
          }
        } else if (duration >= 3000) {
          positioning.resetCalibration();
          showNotice("KALIBRACJA USUNIETA", now);
        } else if (duration >= 1000) {
          positioning.startCalibration();
        } else {
          nextScreen();
        }
      } else if (screen == AccelScreen) {
        if (duration >= 1000) {
          if (tareSet) {
            tareSet = false;
            showNotice("TARA WYLACZONA", now);
          } else if (fresh(now)) {
            tare = latest;
            tareSet = true;
            showNotice("TARA USTAWIONA", now);
          } else showNotice("BRAK DANYCH", now);
        } else {
          nextScreen();
        }
      } else if (!imuEnabled) {
        nextScreen();
      } else if (screen == StrokeScreen) {
        // This screen: a tap clears the counter, a hold moves on.
        if (duration >= 1000) nextScreen();
        else { strokes.reset(); showNotice("LICZNIK SKASOWANY", now); }
      } else if (duration >= 3000) {
        if (zeroed) {
          history.zero({});
          zeroed = false;
          binStart = now;
          showNotice("PELNY ODCZYT", now);
        } else if (fresh(now)) {
          history.zero(latest);
          zeroed = true;
          binStart = now;
          showNotice("USTAWIONO ZERO", now);
        } else showNotice("BRAK DANYCH DO ZERA", now);
      } else if (duration >= 1000) {
        paused = !paused;
        if (!paused) { history.clear(); window.clear(); binStart = now; }
      } else {
        nextScreen();
      }
    }
  }
}

float axis(Accel a, int channel) {
  return channel == 0 ? a.x : channel == 1 ? a.y : a.z;
}
int yPixel(float v) {
  v = fmaxf(-fullScale, fminf(fullScale, v));
  return plotY + lroundf((fullScale - v) * (plotH - 1) / (2 * fullScale));
}
void label(int x, int y, uint16_t color, const char *text) {
  canvas.setCursor(x, y);
  canvas.setTextColor(color);
  canvas.print(text);
}
void drawHeader(uint32_t now, const char *title, uint16_t color) {
  label(0, 0, color, title);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(162, 0);
  canvas.printf("%3.0f Hz", fresh(now) ? actualHz : 0);
  canvas.setTextColor(muted);
  canvas.setCursor(222, 0);
  canvas.printf("%d/%d", screenPos + 1, screenCount);
}
void drawGrid() {
  for (int n = 0; n <= 4; ++n) {
    int y = plotY + n * (plotH - 1) / 4;
    canvas.drawFastHLine(plotX, y, plotW, grid);
    int x = plotX + n * (plotW - 1) / 4;
    canvas.drawFastVLine(x, plotY, plotH, grid);
  }
}
const char *liveState(uint32_t now) {
  return fresh(now) ? (paused ? "PAUZA" : "NA ZYWO") : workerState;
}
void drawTime(uint32_t now) {
  drawHeader(now, "DRGANIA", ST77XX_WHITE);
  label(56, 0, colors[0], "X");
  label(70, 0, colors[1], "Y");
  label(84, 0, colors[2], "Z");
  label(100, 0, muted, "m/s2");
  label(0, 13, fresh(now) ? ST77XX_GREEN : ST77XX_YELLOW, liveState(now));
  label(144, 13, muted, zeroed ? "OFFSET" : "PELNY");
  label(0, plotY, muted, "+20");
  label(18, plotY + plotH / 2 - 3, muted, "0");
  label(0, plotY + plotH - 7, muted, "-20");
  drawGrid();
  bool clipped = false;
  for (int channel = 0; channel < 3; ++channel) {
    bool previous = false;
    int prevX = 0, prevY = 0;
    for (std::size_t i = 0; i < history.size(); ++i) {
      auto b = history.at(i);
      int x = plotX + (bins - history.size() + i) * 2;
      if (!b.valid) { previous = false; continue; }
      float lo = axis(b.low, channel), hi = axis(b.high, channel);
      clipped |= lo < -fullScale || hi > fullScale;
      int top = yPixel(hi), bottom = yPixel(lo);
      if (previous)
        canvas.drawLine(prevX, prevY, x, yPixel(axis(b.first, channel)), colors[channel]);
      canvas.drawFastVLine(x, top, bottom - top + 1, colors[channel]);
      prevX = x; prevY = yPixel(axis(b.last, channel)); previous = true;
    }
  }
  label(plotX, 110, muted, "-4s");
  label(210, 110, muted, "0s");
  if (clipped) label(81, 110, ST77XX_YELLOW, "POZA SKALA");
  else if (dropped.load()) label(81, 110, ST77XX_YELLOW, "UTRATA PROBEK");
}

void drawSpectrum(uint32_t now, int channel) {
  static const char *titles[] = {"FFT OS X", "FFT OS Y", "FFT OS Z"};
  drawHeader(now, titles[channel], colors[channel]);
  label(64, 0, muted, "amplituda m/s2");
  drawGrid();
  const bool live = fresh(now) || paused;
  if (!live || !window.full()) {
    label(0, 13, fresh(now) ? ST77XX_GREEN : ST77XX_YELLOW, liveState(now));
    canvas.setTextColor(muted);
    canvas.setCursor(plotX + 50, plotY + plotH / 2 - 3);
    if (live) canvas.printf("ZBIERANIE %u/%u", unsigned(window.size()), unsigned(fftSize));
    else canvas.print("BRAK DANYCH");
  } else {
    amplitudeSpectrum(window, channel, spectrum);
    const float rate = window.rateHz();
    std::size_t best = 1;
    for (std::size_t k = 1; k <= fftSize / 2; ++k)
      if (spectrum[k] > spectrum[best]) best = k;
    canvas.setTextColor(paused ? ST77XX_YELLOW : ST77XX_GREEN);
    canvas.setCursor(0, 13);
    canvas.printf("%s  max %.1f Hz  %.2f", paused ? "PAUZA" : "NA ZYWO",
                  best * rate / fftSize, spectrum[best]);
    canvas.setTextColor(muted);
    canvas.setCursor(0, plotY);
    canvas.printf("%.0f", fftRange);
    label(18, plotY + plotH - 7, muted, "0");
    // DC is removed, so bin 0 is skipped; one line vertex per bin up to Nyquist.
    int prevX = 0, prevY = 0;
    for (std::size_t k = 1; k <= fftSize / 2; ++k) {
      int x = plotX + k * (plotW - 1) / (fftSize / 2);
      int y = plotY + plotH - 1 - lroundf(fminf(spectrum[k] / fftRange, 1.0f) * (plotH - 1));
      if (k > 1) canvas.drawLine(prevX, prevY, x, y, colors[channel]);
      prevX = x; prevY = y;
    }
    canvas.setCursor(plotX, 110);
    canvas.print("0");
    canvas.setCursor(plotX + plotW / 2 - 6, 110);
    canvas.printf("%.0f", rate / 4);
    canvas.setCursor(198, 110);
    canvas.printf("%.0f Hz", rate / 2);
  }
}

void drawStrokes(uint32_t now) {
  drawHeader(now, "LICZNIK RUCHOW", ST77XX_WHITE);
  label(0, 13, fresh(now) ? ST77XX_GREEN : ST77XX_YELLOW, liveState(now));
  canvas.setTextSize(5);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(0, 26);
  canvas.printf("%lu", (unsigned long)strokes.count());
  canvas.setTextSize(2);
  canvas.setTextColor(strokes.perMinute() > 0 ? ST77XX_GREEN : muted);
  canvas.setCursor(130, 28);
  canvas.printf("%.0f/min", strokes.perMinute());
  canvas.setTextSize(1);
  label(130, 48, muted, "pelnych ruchow");
  label(130, 58, muted, "(tam i z powrotem)");
  // Tracked stroke direction in sensor axes, so the grip does not matter.
  canvas.setCursor(0, 72);
  canvas.setTextColor(muted);
  canvas.printf("kierunek X%+.1f Y%+.1f Z%+.1f", strokes.axis(0), strokes.axis(1), strokes.axis(2));
  // Velocity-like signal against the detection threshold: +-range spans the bar.
  const int barX = 2, barW = 236, mid = barX + barW / 2, barY = 88, barH = 14;
  const float range = fmaxf(strokes.envelope(), 0.1f);
  canvas.drawRect(barX, barY, barW, barH, muted);
  const int tick = lroundf(fminf(strokes.threshold() / range, 1.0f) * (barW / 2 - 2));
  canvas.drawFastVLine(mid - tick, barY, barH, ST77XX_YELLOW);
  canvas.drawFastVLine(mid + tick, barY, barH, ST77XX_YELLOW);
  const int len = lroundf(fmaxf(-1.0f, fminf(1.0f, strokes.filtered() / range)) * (barW / 2 - 2));
  if (len >= 0) canvas.fillRect(mid, barY + 2, len, barH - 4, colors[0]);
  else canvas.fillRect(mid + len, barY + 2, -len, barH - 4, colors[0]);
  canvas.setCursor(0, 108);
  canvas.setTextColor(muted);
  canvas.printf("prog %.2f m/s   szczyt %.2f m/s", strokes.threshold(), strokes.envelope());
}
void drawBattery(uint32_t now) {
  drawHeader(now, "AKUMULATOR", ST77XX_WHITE);
  BatteryInfo b;
  portENTER_CRITICAL(&batteryMux);
  b = battery;
  portEXIT_CRITICAL(&batteryMux);
  if (b.gauge == Gauge::None || !b.valid) {
    label(0, 40, ST77XX_YELLOW, "BRAK ODCZYTU UKLADU POMIARU");
    label(0, 56, muted, "MAX17048 / LC709203F na I2C");
    return;
  }
  canvas.setTextSize(4);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(0, 22);
  canvas.printf("%.0f%%", fminf(b.percent, 100));
  canvas.setTextSize(2);
  canvas.setCursor(140, 30);
  canvas.printf("%.2f V", b.volts);
  canvas.setTextSize(1);
  const int barW = 236, barH = 12;
  canvas.drawRect(0, 62, barW, barH, muted);
  int fill = lroundf(fminf(fmaxf(b.percent, 0), 100) * (barW - 4) / 100);
  canvas.fillRect(2, 64, fill, barH - 4, b.percent < 20 ? ST77XX_RED : ST77XX_GREEN);
  if (b.gauge == Gauge::Max17048) {
    // CRATE sign is the only charge indication: the charger is not on a GPIO.
    const char *state = b.rate > 0.5f ? "LADOWANIE" :
                        b.rate < -0.5f ? "ROZLADOWANIE" :
                        b.percent >= 98 ? "NALADOWANY" : "BEZ ZMIAN";
    canvas.setTextSize(2);
    canvas.setTextColor(b.rate > 0.5f ? ST77XX_GREEN : ST77XX_WHITE);
    canvas.setCursor(0, 82);
    canvas.print(state);
    canvas.setTextSize(1);
    canvas.setTextColor(muted);
    canvas.setCursor(0, 104);
    canvas.printf("%+.1f %%/h   MAX17048", b.rate);
  } else {
    label(0, 84, muted, "LC709203F: stan ladowania");
    label(0, 96, muted, "niedostepny na tej wersji plytki");
  }
}

// Values are smoothed a little (about 0.15 s) so the digits stay readable.
void drawAccel(uint32_t now) {
  AccelScreenState state;
  state.live = fresh(now);
  if (state.live) {
    const float k = 0.3f;
    const Accel in{latest.x - (tareSet ? tare.x : 0), latest.y - (tareSet ? tare.y : 0),
                   latest.z - (tareSet ? tare.z : 0)};
    shown = {shown.x + k * (in.x - shown.x), shown.y + k * (in.y - shown.y), shown.z + k * (in.z - shown.z)};
  }
  state.value = shown;
  state.tared = tareSet;
  state.rateHz = actualHz;
  state.status = workerState;
  state.screenPos = screenPos;
  state.screenCount = screenCount;
  drawAccelScreen(canvas, state);
}

NavScreenState navState(uint32_t now) {
  NavScreenState n;
  n.progress = positioning.getCalibrationProgress();
  n.position = positioning.getPosition();
  n.calibrated = positioning.isCalibrated();
  n.sideM = positioning.sideM();
  for (uint8_t i = 0; i < 3; ++i) positioning.referencePoint(i, n.refX[i], n.refY[i]);
  n.screenPos = screenPos;
  n.screenCount = screenCount;
  n.buttonHeld = buttonStable;
  n.heldMs = now - buttonDown;
  n.notice = int32_t(noticeUntil - now) > 0 ? notice : nullptr;
  return n;
}

void draw(uint32_t now) {
  if (ipsEnabled && screen == NavScreen) {
    const NavScreenState state = navState(now);
    const uint32_t signature = navSignature(state, now);
    if (navDrawn && signature == lastNavSignature) return; // Nothing changed: no SPI traffic.
    lastNavSignature = signature;
    navDrawn = true;
    drawNavigationScreen(canvas, state, now);
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
    return;
  }
  canvas.fillScreen(bg);
  if (screen == TimeScreen) drawTime(now);
  else if (screen == AccelScreen) drawAccel(now);
  else if (screen == StrokeScreen) drawStrokes(now);
  else if (screen == BatteryScreen) drawBattery(now);
  else drawSpectrum(now, screen - FftX);
  if (buttonStable && screen == AccelScreen) {
    label(0, 125, ST77XX_YELLOW, now - buttonDown >= 1000 ? (tareSet ? "PUSC: WYLACZ TARE" : "PUSC: USTAW TARE") : "PUSC: NASTEPNY EKRAN");
  } else if (buttonStable && screen == StrokeScreen) {
    label(0, 125, ST77XX_YELLOW, now - buttonDown >= 1000 ? "PUSC: NASTEPNY EKRAN" : "PUSC: SKASUJ LICZNIK");
  } else if (buttonStable) {
    uint32_t held = now - buttonDown;
    label(0, 125, ST77XX_YELLOW, held >= 3000 ? (zeroed ? "PUSC: PELNY ODCZYT" : "PUSC: USTAW ZERO") :
          held >= 1000 ? "PUSC: PAUZA / 3s: ZERO" : "PUSC: NASTEPNY EKRAN");
  } else if (int32_t(noticeUntil - now) > 0)
    label(0, 125, ST77XX_YELLOW, notice);
  else if (screen == AccelScreen) label(0, 125, muted, "BOOT: ekran / 1s: tara");
  else if (screen == StrokeScreen) label(0, 125, muted, "BOOT: kasuj / 1s: nastepny ekran");
  else if (!imuEnabled) label(0, 125, muted, "BOOT: nastepny ekran");
  else label(0, 125, muted, "BOOT: ekran / 1s:pauza / 3s:zero");
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
}
}

void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  pinMode(mpuIntPin, INPUT_PULLDOWN); // MPU6050 INT is push-pull, active high.
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  Wire.begin(SDA, SCL, i2cHz); // MPU6050 and fuel gauge; the sensor task reopens it on reconnect.
  Wire.setTimeOut(20);
  SPI.begin(SCK, MISO, MOSI);
  tft.init(135, 240);
  tft.setRotation(3);
  tft.setSPISpeed(32000000);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  if (!canvas.getBuffer()) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(ST77XX_RED);
    tft.print("Brak RAM dla wykresu");
    while (true) delay(1000);
  }
  binStart = lastFrame = rateStart = millis();
  draw(millis());
  if (ipsEnabled) positioning.begin();
  events = xQueueCreate(256, sizeof(Event));
  if (!events || xTaskCreate(sensorTask, "MPU6050", 8192, nullptr, 1, &sensorHandle) != pdPASS) {
    workerState = "BLAD PAMIECI / TASK";
    return;
  }
}

void loop() {
  Event e{};
  if (events) {
    while (xQueueReceive(events, &e, 0) == pdTRUE) {
      advanceTo(e.ms);
      if (e.kind == Kind::Sample) {
        latest = e.value;
        lastSample = e.ms;
        haveSample = true;
        ++sampleCount;
        workerState = "BRAK DANYCH";
        if (imuEnabled) {
          strokes.setBiasTau(1.0f); // Plain accelerometer: gravity must be filtered out.
          strokes.add(e.value, e.us);
          if (!paused) {
            history.add(e.value);
            // A gap in reports would smear the spectrum: start a new window.
            if (window.size() && uint32_t(e.us - window.newestUs()) > fftGapMs * 1000)
              window.clear();
            window.add(e.value, e.us);
          }
        }
      } else {
        history.discardPending();
        strokes.breakSignal();
        haveSample = false;
        workerState = e.kind == Kind::Connecting ? "LACZENIE MPU6050" : "BRAK MPU6050 (I2C 0x68/0x69)";
      }
    }
  }
  uint32_t now = millis();
  advanceTo(now);
  pollButton(now);
  // Scan only while the map is shown (or a calibration runs from it).
  if (ipsEnabled) {
    if (screen == NavScreen) positioning.startPositioning();
    else positioning.stopPositioning();
    positioning.loop(now);
  }
  if (uint32_t(now - rateStart) >= 1000) {
    actualHz = sampleCount * 1000.0f / uint32_t(now - rateStart);
    sampleCount = 0;
    rateStart = now;
    Serial.printf("rate=%.1f Hz, drops=%lu, state=%s\n",
                  actualHz, (unsigned long)dropped.load(), fresh(now) ? "live" : workerState);
    if (fresh(now))
      Serial.printf("accel_xyz=%.3f,%.3f,%.3f m/s2\n", latest.x, latest.y, latest.z);
  }
  if (uint32_t(now - lastFrame) >= binMs) { lastFrame = now; draw(now); }
  delay(1);
}



