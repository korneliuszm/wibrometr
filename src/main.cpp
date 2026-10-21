#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>
#include <atomic>
#include <cmath>
#include <iterator>
#include "SignalHistory.h"
#include "Spectrum.h"
#include "StrokeCounter.h"
#include "RestartableBNO08x.h"
#include "indoor_positioning/IndoorPositioning.h"
#include "NavigationScreen.h"
#include "BleScreen.h"
#include "ble_ranging/BleDistance.h"
#include "UiTheme.h"

namespace {
// Accelerometer (BNO085) and its screens are switched off while the Wi-Fi
// indoor positioning prototype is evaluated; set true to bring them back.
constexpr bool imuEnabled = false;
// Wi-Fi indoor positioning prototype: off (too coarse in practice); the code
// stays, set true to bring back its map screen.
constexpr bool ipsEnabled = false;
// Distance to one BLE device from its RSSI.
constexpr bool bleEnabled = true;
constexpr uint32_t binMs = 40, reportUs = 6667, staleMs = 1000;
// Raw accelerometer (ADC counts, scale assumes +-8 g over 16 bits). Tested on
// BNO085 firmware 3.2.13: enabling it succeeds but the hub sends no reports,
// so it stays off.
constexpr bool useRaw = false;
constexpr sh2_SensorId_t imuSensor = useRaw ? SH2_RAW_ACCELEROMETER : SH2_ACCELEROMETER;
constexpr float rawToMs2 = 9.80665f / 4096.0f;
constexpr uint32_t reconnectMs = 200;
// Gravity-free acceleration from the hub's fusion, used only by the stroke
// counter: tilting the hand moves gravity between axes as much as a stroke does.
constexpr uint32_t linearUs = 10000; // Delay before re-initialising a stalled sensor.
constexpr uint32_t retryMs = 3000;
// BNO085 on the shared SPI bus (SCK/MOSI/MISO = GPIO36/35/37).
constexpr uint8_t imuCs = A5, imuInt = A4, imuRst = A3, imuWake = A2;
constexpr int bins = 100;
constexpr std::size_t fftSize = 512; // ~3.4 s at 150 Hz, ~0.3 Hz per bin.
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
RestartableBNO08x imu(imuRst);
enum Screen { TimeScreen, FftX, FftY, FftZ, StrokeScreen, NavScreen, BleScreen, BatteryScreen };
// Screens of the enabled features, in BOOT order; battery is always last.
constexpr int countScreens() { return (imuEnabled ? 5 : 0) + ipsEnabled + bleEnabled + 1; }
constexpr int screenCount = countScreens();
struct ScreenList {
  Screen s[screenCount] = {};
  constexpr ScreenList() {
    int n = 0;
    if (imuEnabled)
      for (Screen x : {TimeScreen, FftX, FftY, FftZ, StrokeScreen}) s[n++] = x;
    if (ipsEnabled) s[n++] = NavScreen;
    if (bleEnabled) s[n++] = BleScreen;
    s[n] = BatteryScreen;
  }
};
constexpr ScreenList activeScreens;
int screenPos = 0; // Index into activeScreens.
Screen screen = activeScreens.s[0];
ips::IndoorPositioning positioning;
ble::BleDistance bleDistance;
uint32_t lastBleFrame = 0;
uint32_t lastNavSignature = 0;
bool navDrawn = false;
SampleWindow<fftSize> window;
StrokeCounter strokes; // Hand strokes in any direction; fed regardless of pause.
float spectrum[fftSize / 2 + 1];
// Feather 5483 carries a MAX17048 (newer) or LC709203F (older) fuel gauge.
enum class Gauge { None, Max17048, Lc709203f };
Gauge gauge = Gauge::None;
Adafruit_LC709203F lc;
bool batteryValid = false;
float batteryVolts = 0, batteryPercent = 0, batteryRate = 0;
uint32_t batteryAt = 0, gaugeProbeAt = 0;
enum class Kind { Sample, Linear, Connecting, Waiting, Reset, NoInt, NoSpi };
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
const char *workerState = "SZUKAM BNO085";
uint32_t noticeUntil = 0;
const char *notice = "";

std::atomic<uint32_t> lastGoodMs{0};
std::atomic<bool> linearActive{false};

void emit(Kind kind, Accel value = {}, uint32_t us = 0) {
  Event event{kind, millis(), us, value};
  if (xQueueSend(events, &event, 0) != pdTRUE) ++dropped;
}

// Called by SH2 once per report. The hub packs several reports into one SHTP
// packet at high rates and the library's own handler keeps only the last, so
// every report is forwarded here with the sensor's own timestamp.
void onSensorEvent(void *, sh2_SensorEvent_t *event) {
  sh2_SensorValue_t value{};
  if (sh2_decodeSensorEvent(&value, event) != SH2_OK) return;
  if (value.sensorId == SH2_LINEAR_ACCELERATION) {
    const auto &l = value.un.linearAcceleration;
    if (std::isfinite(l.x) && std::isfinite(l.y) && std::isfinite(l.z))
      emit(Kind::Linear, {l.x, l.y, l.z}, uint32_t(value.timestamp));
    return;
  }
  if (value.sensorId != imuSensor) return;
  Accel a;
  uint32_t us = uint32_t(value.timestamp);
  if (useRaw) {
    const auto &r = value.un.rawAccelerometer;
    a = {r.x * rawToMs2, r.y * rawToMs2, r.z * rawToMs2};
    us = r.timestamp ? r.timestamp : us; // Sensor's own sample time, when present.
  } else {
    a = {value.un.accelerometer.x, value.un.accelerometer.y, value.un.accelerometer.z};
  }
  if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z)) return;
  lastGoodMs = millis();
  emit(Kind::Sample, a, us);
}

// Main report is required; linear acceleration is optional and the stroke
// counter falls back to the plain accelerometer without it.
bool enableReports() {
  linearActive = imu.enableReport(SH2_LINEAR_ACCELERATION, linearUs);
  Serial.printf("BNO085: linear acceleration %s\n", linearActive ? "on" : "unavailable");
  return imu.enableReport(imuSensor, reportUs);
}

// Only this task owns the SH2 driver. The SPI bus is shared with the TFT;
// Arduino-ESP32 transactions serialize both tasks. Library initialization can
// block briefly; TFT and buttons continue to run on the Arduino loop task.
void sensorTask(void *) {
  bool ready = false;
  uint32_t retryAt = 0;
  for (;;) {
    uint32_t now = millis();
    if (!ready) {
      if (int32_t(now - retryAt) >= 0) {
        emit(Kind::Connecting);
        // Diagnose before the driver: after RST the BNO085 boots and pulls INT
        // low to send its advertisement, in I2C and SPI mode alike.
        pinMode(imuInt, INPUT_PULLUP);
        pinMode(imuWake, OUTPUT);
        digitalWrite(imuWake, HIGH); // PS0 high at reset selects SPI mode.
        pinMode(imuRst, OUTPUT);
        digitalWrite(imuRst, LOW);
        delay(10);
        digitalWrite(imuRst, HIGH);
        uint32_t start = millis();
        while (digitalRead(imuInt) == HIGH && uint32_t(millis() - start) < 1000) delay(1);
        const bool intSeen = digitalRead(imuInt) == LOW;
        Serial.printf("BNO085: INT after reset %s (%lu ms)\n", intSeen ? "LOW" : "never LOW",
                      (unsigned long)(millis() - start));
        if (!intSeen) {
          Serial.println("BNO085: check INT->A4, RST->A3, 3V, GND");
          emit(Kind::NoInt);
        // beginSpi pulses RST again before opening SH2, so retries start clean.
        } else if (!imu.beginSpi(imuCs, imuInt, imuWake, &SPI)) {
          Serial.println("BNO085: INT ok, no SPI reply: check PS0->A2, PS1 at 3V, SCK, MOSI (DI/AD0), MISO (SDA), CS->A5");
          emit(Kind::NoSpi);
        } else {
          sh2_setSensorCallback(onSensorEvent, nullptr);
          if (enableReports()) {
            imu.wasReset();
            ready = true;
            lastGoodMs = millis();
            Serial.printf("BNO085 SPI: part %lu sw %u.%u.%u, accelerometer requested %lu Hz\n",
                          (unsigned long)imu.prodIds.entry[0].swPartNumber,
                          imu.prodIds.entry[0].swVersionMajor, imu.prodIds.entry[0].swVersionMinor,
                          imu.prodIds.entry[0].swVersionPatch,
                          (unsigned long)(1000000UL / reportUs));
          } else {
            Serial.println("BNO085: SPI ok, enabling accelerometer failed");
            emit(Kind::Waiting);
          }
        }
        retryAt = millis() + retryMs;
      }
    } else {
      if (imu.wasReset()) {
        emit(Kind::Reset);
        ready = enableReports();
        lastGoodMs = millis();
      }

      // INT low means data is waiting; otherwise the SPI HAL would block and
      // hardware-reset the sensor after 500 ms.
      if (ready && digitalRead(imuInt) == LOW) sh2_service(); // Reports arrive in onSensorEvent.
      if (!ready || uint32_t(millis() - lastGoodMs) > staleMs) {
        const auto &st = RestartableBNO08x::stats;
        Serial.printf("BNO085: reports stopped; INT=%d reads=%lu badHdr=%lu (%02X %02X %02X %02X) wrTimeouts=%lu lateInt=%lu\n",
                      digitalRead(imuInt), (unsigned long)st.reads, (unsigned long)st.badHeaders,
                      st.lastBad[0], st.lastBad[1], st.lastBad[2], st.lastBad[3],
                      (unsigned long)st.writeTimeouts, (unsigned long)st.lateInt);
        // Last packet headers: channel (byte 2) 3 = sensor reports, 0/1 = hub
        // housekeeping, FF FF = no data. Oldest first, ms before now.
        uint32_t nowMs = millis();
        for (int n = 0; n < Bno08xLinkStats::ringSize; ++n) {
          int i = (st.head + n) % Bno08xLinkStats::ringSize;
          if (!st.ringMs[i]) continue;
          Serial.printf("  -%lu ms: %02X %02X %02X %02X\n", (unsigned long)(nowMs - st.ringMs[i]),
                        st.ring[i][0], st.ring[i][1], st.ring[i][2], st.ring[i][3]);
        }
        // Is the hub alive but silent? Request wake and see if it raises INT.
        digitalWrite(imuWake, LOW);
        uint32_t t0 = millis();
        while (digitalRead(imuInt) == HIGH && uint32_t(millis() - t0) < 100) delay(1);
        Serial.printf("BNO085: WAKE probe: INT %s after %lu ms\n",
                      digitalRead(imuInt) == LOW ? "LOW (hub alive)" : "HIGH (hub silent)",
                      (unsigned long)(millis() - t0));
        digitalWrite(imuWake, HIGH);
        ready = false;
        emit(Kind::Waiting);
        retryAt = millis() + reconnectMs;
      }
    }
    // At 150 Hz and up a fixed 1 ms sleep per packet is too slow; sleep only while
    // the hub has nothing pending.
    if (!ready || digitalRead(imuInt) == HIGH) vTaskDelay(pdMS_TO_TICKS(1));
    else taskYIELD();
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
      } else if (bleEnabled && screen == BleScreen) {
        static char text[40];
        if (duration >= 3000) {
          float rssi = 0;
          if (bleDistance.calibrate1m(now, rssi)) {
            snprintf(text, sizeof text, "1 m = %.0f dBm ZAPISANE", rssi);
            showNotice(text, now);
          } else showNotice("BRAK SYGNALU CELU", now);
        } else if (duration >= 1000) {
          ble::Address adopted;
          if (bleDistance.adoptStrongest(now, adopted)) {
            char addr[18];
            adopted.format(addr, sizeof addr);
            snprintf(text, sizeof text, "NOWY CEL %s", addr);
            showNotice(text, now);
          } else showNotice("BRAK INNYCH URZADZEN", now);
        } else {
          nextScreen();
        }
      } else if (!imuEnabled) {
        nextScreen(); // Pause and zero belong to the accelerometer screens.
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
  if (imuEnabled) {
    canvas.setTextColor(ST77XX_WHITE);
    canvas.setCursor(162, 0);
    canvas.printf("%3.0f Hz", fresh(now) ? actualHz : 0);
  }
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

bool readGaugeWord(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(0x36);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(uint8_t(0x36), uint8_t(2)) != 2)
    return false;
  value = uint16_t(Wire.read()) << 8;
  value |= Wire.read();
  return true;
}
// Runs on the loop task, which alone owns Wire. MAX17048 is read directly so
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
  if (gauge == Gauge::Max17048) {
    uint16_t vcell, soc, crate;
    batteryValid = readGaugeWord(0x02, vcell) && readGaugeWord(0x04, soc) &&
                   readGaugeWord(0x16, crate);
    if (batteryValid) {
      batteryVolts = vcell * 78.125e-6f;
      batteryPercent = soc / 256.0f;
      batteryRate = int16_t(crate) * 0.208f;
    }
  } else {
    batteryVolts = lc.cellVoltage();
    batteryPercent = lc.cellPercent();
    batteryValid = std::isfinite(batteryVolts) && batteryVolts > 0;
  }
  if (!batteryValid) { gauge = Gauge::None; gaugeProbeAt = now + gaugeRetryMs; }
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
  canvas.setTextColor(linearActive ? muted : ST77XX_YELLOW);
  if (linearActive)
    canvas.printf("kierunek X%+.1f Y%+.1f Z%+.1f", strokes.axis(0), strokes.axis(1), strokes.axis(2));
  else canvas.print("BEZ przysp. liniowego (slabiej)");
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
  if (gauge == Gauge::None || !batteryValid) {
    label(0, 40, ST77XX_YELLOW, "BRAK ODCZYTU UKLADU POMIARU");
    label(0, 56, muted, "MAX17048 / LC709203F na I2C");
    return;
  }
  canvas.setTextSize(4);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(0, 22);
  canvas.printf("%.0f%%", fminf(batteryPercent, 100));
  canvas.setTextSize(2);
  canvas.setCursor(140, 30);
  canvas.printf("%.2f V", batteryVolts);
  canvas.setTextSize(1);
  const int barW = 236, barH = 12;
  canvas.drawRect(0, 62, barW, barH, muted);
  int fill = lroundf(fminf(fmaxf(batteryPercent, 0), 100) * (barW - 4) / 100);
  canvas.fillRect(2, 64, fill, barH - 4, batteryPercent < 20 ? ST77XX_RED : ST77XX_GREEN);
  if (gauge == Gauge::Max17048) {
    // CRATE sign is the only charge indication: the charger is not on a GPIO.
    const char *state = batteryRate > 0.5f ? "LADOWANIE" :
                        batteryRate < -0.5f ? "ROZLADOWANIE" :
                        batteryPercent >= 98 ? "NALADOWANY" : "BEZ ZMIAN";
    canvas.setTextSize(2);
    canvas.setTextColor(batteryRate > 0.5f ? ST77XX_GREEN : ST77XX_WHITE);
    canvas.setCursor(0, 82);
    canvas.print(state);
    canvas.setTextSize(1);
    canvas.setTextColor(muted);
    canvas.setCursor(0, 104);
    canvas.printf("%+.1f %%/h   MAX17048", batteryRate);
  } else {
    label(0, 84, muted, "LC709203F: stan ladowania");
    label(0, 96, muted, "niedostepny na tej wersji plytki");
  }
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
  if (bleEnabled && screen == BleScreen) {
    // RSSI changes with every advert; 4 frames/s is plenty and saves the bus.
    if (uint32_t(now - lastBleFrame) < 250 && !buttonStable) return;
    lastBleFrame = now;
    BleScreenState state;
    state.view = bleDistance.view(now);
    state.screenPos = screenPos;
    state.screenCount = screenCount;
    state.buttonHeld = buttonStable;
    state.heldMs = now - buttonDown;
    state.notice = int32_t(noticeUntil - now) > 0 ? notice : nullptr;
    drawBleScreen(canvas, state);
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 240, 135);
    return;
  }
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
  else if (screen == StrokeScreen) drawStrokes(now);
  else if (screen == BatteryScreen) drawBattery(now);
  else drawSpectrum(now, screen - FftX);
  if (buttonStable && screen == StrokeScreen) {
    label(0, 125, ST77XX_YELLOW, now - buttonDown >= 1000 ? "PUSC: NASTEPNY EKRAN" : "PUSC: SKASUJ LICZNIK");
  } else if (buttonStable) {
    uint32_t held = now - buttonDown;
    label(0, 125, ST77XX_YELLOW, held >= 3000 ? (zeroed ? "PUSC: PELNY ODCZYT" : "PUSC: USTAW ZERO") :
          held >= 1000 ? "PUSC: PAUZA / 3s: ZERO" : "PUSC: NASTEPNY EKRAN");
  } else if (int32_t(noticeUntil - now) > 0)
    label(0, 125, ST77XX_YELLOW, notice);
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
  pinMode(imuCs, OUTPUT);
  digitalWrite(imuCs, HIGH); // Keep BNO085 off the bus while the TFT starts.
  pinMode(imuWake, OUTPUT);
  digitalWrite(imuWake, HIGH);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  Wire.begin(SDA, SCL, 100000); // Fuel gauge only; the IMU is on SPI.
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
  if (bleEnabled) bleDistance.begin();
  if (!imuEnabled) return;
  events = xQueueCreate(256, sizeof(Event));
  if (!events || xTaskCreate(sensorTask, "BNO085", 8192, nullptr, 1, nullptr) != pdPASS) {
    workerState = "BLAD PAMIECI / TASK";
    return;
  }
}

void loop() {
  Event e{};
  if (events) {
    while (xQueueReceive(events, &e, 0) == pdTRUE) {
      advanceTo(e.ms);
      if (e.kind == Kind::Linear) {
        strokes.setBiasTau(2.0f);
        strokes.add(e.value, e.us);
      } else if (e.kind == Kind::Sample) {
        latest = e.value;
        lastSample = e.ms;
        haveSample = true;
        ++sampleCount;
        workerState = "BRAK DANYCH";
        if (!linearActive) { strokes.setBiasTau(1.0f); strokes.add(e.value, e.us); }
        if (!paused) {
          history.add(e.value);
          // A gap in reports would smear the spectrum: start a new window.
          if (window.size() && uint32_t(e.us - window.newestUs()) > fftGapMs * 1000)
            window.clear();
          window.add(e.value, e.us);
        }
      } else {
        history.discardPending();
        strokes.breakSignal();
        haveSample = false;
        workerState = e.kind == Kind::Connecting ? "LACZENIE BNO085" :
                      e.kind == Kind::Reset ? "RESET CZUJNIKA" :
                      e.kind == Kind::NoInt ? "BNO: BRAK INT (A4/A3)" :
                      e.kind == Kind::NoSpi ? "BNO: BRAK SPI (PS0=A2?)" :
                      "BRAK BNO085 / RETRY";
      }
    }
  }
  uint32_t now = millis();
  advanceTo(now);
  pollButton(now);
  pollBattery(now);
  // Scan only while the map is shown (or a calibration runs from it).
  if (ipsEnabled) {
    if (screen == NavScreen) positioning.startPositioning();
    else positioning.stopPositioning();
    positioning.loop(now);
  }
  if (bleEnabled) bleDistance.loop(now);
  if (imuEnabled && uint32_t(now - rateStart) >= 1000) {
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



