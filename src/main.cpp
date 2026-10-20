#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>
#include <atomic>
#include <cmath>
#include "SignalHistory.h"
#include "Spectrum.h"
#include "RestartableBNO08x.h"

namespace {
constexpr uint32_t binMs = 40, reportUs = 10000, staleMs = 1000;
constexpr uint32_t reconnectMs = 200; // Delay before re-initialising a stalled sensor.
constexpr uint32_t retryMs = 3000;
// BNO085 on the shared SPI bus (SCK/MOSI/MISO = GPIO36/35/37).
constexpr uint8_t imuCs = A5, imuInt = A4, imuRst = A3, imuWake = A2;
constexpr int bins = 100;
constexpr std::size_t fftSize = 256; // ~2.6 s at 100 Hz, ~0.4 Hz per bin.
constexpr uint32_t fftGapMs = 100, batteryMs = 2000, gaugeRetryMs = 5000;
constexpr float fullScale = 20.0f;
constexpr int plotX = 30, plotY = 28, plotW = 200, plotH = 79;
constexpr uint16_t bg = 0x0841, grid = 0x2945, muted = 0x9CF3;
constexpr uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, 0x3DFF};
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);
SignalHistory<bins> history;
RestartableBNO08x imu(imuRst);
enum Screen { TimeScreen, FftX, FftY, FftZ, BatteryScreen, ScreenCount };
int screen = TimeScreen;
SampleWindow<fftSize> window;
float spectrum[fftSize / 2 + 1];
float fftScale[3] = {0.1f, 0.1f, 0.1f};
uint32_t fftLowSince[3] = {};
// Feather 5483 carries a MAX17048 (newer) or LC709203F (older) fuel gauge.
enum class Gauge { None, Max17048, Lc709203f };
Gauge gauge = Gauge::None;
Adafruit_LC709203F lc;
bool batteryValid = false;
float batteryVolts = 0, batteryPercent = 0, batteryRate = 0;
uint32_t batteryAt = 0, gaugeProbeAt = 0;
enum class Kind { Sample, Connecting, Waiting, Reset, NoInt, NoSpi };
struct Event { Kind kind; uint32_t ms; Accel value; };
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

void emit(Kind kind, Accel value = {}) {
  Event event{kind, millis(), value};
  if (xQueueSend(events, &event, 0) != pdTRUE) ++dropped;
}

// Only this task owns the SH2 driver. The SPI bus is shared with the TFT;
// Arduino-ESP32 transactions serialize both tasks. Library initialization can
// block briefly; TFT and buttons continue to run on the Arduino loop task.
void sensorTask(void *) {
  sh2_SensorValue_t report{}; // SH2 retains this pointer: keep it alive for the task.
  bool ready = false;
  uint32_t lastGood = 0, retryAt = 0;
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
          imu.getSensorEvent(&report); // Register the persistent callback target.
          if (imu.enableReport(SH2_ACCELEROMETER, reportUs)) {
            imu.wasReset();
            ready = true;
            lastGood = millis();
            Serial.printf("BNO085 SPI: part %lu sw %u.%u.%u, accelerometer requested 100 Hz\n",
                          (unsigned long)imu.prodIds.entry[0].swPartNumber,
                          imu.prodIds.entry[0].swVersionMajor, imu.prodIds.entry[0].swVersionMinor,
                          imu.prodIds.entry[0].swVersionPatch);
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
        ready = imu.enableReport(SH2_ACCELEROMETER, reportUs);
        lastGood = millis();
      }

      // INT low means data is waiting; otherwise the SPI HAL would block and
      // hardware-reset the sensor after 500 ms.
      if (ready && digitalRead(imuInt) == LOW && imu.getSensorEvent(&report) &&
          report.sensorId == SH2_ACCELEROMETER) {
        Accel a{report.un.accelerometer.x, report.un.accelerometer.y,
                report.un.accelerometer.z};
        if (std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z)) {
          lastGood = millis();
          emit(Kind::Sample, a);
        }
      }
      if (!ready || uint32_t(millis() - lastGood) > staleMs) {
        const auto &st = RestartableBNO08x::stats;
        Serial.printf("BNO085: reports stopped; INT=%d reads=%lu badHdr=%lu (%02X %02X %02X %02X) wrTimeouts=%lu lateInt=%lu\n",
                      digitalRead(imuInt), (unsigned long)st.reads, (unsigned long)st.badHeaders,
                      st.lastBad[0], st.lastBad[1], st.lastBad[2], st.lastBad[3],
                      (unsigned long)st.writeTimeouts, (unsigned long)st.lateInt);
        ready = false;
        emit(Kind::Waiting);
        retryAt = millis() + reconnectMs;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
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
      if (duration >= 3000) {
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
        screen = (screen + 1) % ScreenCount;
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
  canvas.printf("%d/%d", screen + 1, int(ScreenCount));
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

// Smallest 1-2-5 step above the peak; grows at once, shrinks after 2 s.
float fftRange(int channel, float peak, uint32_t now) {
  static constexpr float steps[] = {0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1, 2, 5, 10, 20};
  float target = steps[9];
  for (float step : steps)
    if (peak * 1.1f <= step) { target = step; break; }
  float &scale = fftScale[channel];
  if (target >= scale) { scale = target; fftLowSince[channel] = now; }
  else if (uint32_t(now - fftLowSince[channel]) >= 2000) {
    scale = target;
    fftLowSince[channel] = now;
  }
  return scale;
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
    const float range = fftRange(channel, spectrum[best], now);
    canvas.setTextColor(paused ? ST77XX_YELLOW : ST77XX_GREEN);
    canvas.setCursor(0, 13);
    canvas.printf("%s  max %.1f Hz  %.2f", paused ? "PAUZA" : "NA ZYWO",
                  best * rate / fftSize, spectrum[best]);
    canvas.setTextColor(muted);
    canvas.setCursor(0, plotY);
    canvas.printf(range < 0.1f ? "%.2f" : range < 1 ? "%.1f" : "%.0f", range);
    label(18, plotY + plotH - 7, muted, "0");
    // DC is removed, so bin 0 is skipped; one stem per bin up to Nyquist.
    for (std::size_t k = 1; k <= fftSize / 2; ++k) {
      int x = plotX + k * (plotW - 1) / (fftSize / 2);
      int h = lroundf(fminf(spectrum[k] / range, 1.0f) * (plotH - 1));
      if (h > 0)
        canvas.drawFastVLine(x, plotY + plotH - 1 - h, h + 1, colors[channel]);
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

void draw(uint32_t now) {
  canvas.fillScreen(bg);
  if (screen == TimeScreen) drawTime(now);
  else if (screen == BatteryScreen) drawBattery(now);
  else drawSpectrum(now, screen - FftX);
  if (buttonStable) {
    uint32_t held = now - buttonDown;
    label(0, 125, ST77XX_YELLOW, held >= 3000 ? (zeroed ? "PUSC: PELNY ODCZYT" : "PUSC: USTAW ZERO") :
          held >= 1000 ? "PUSC: PAUZA / 3s: ZERO" : "PUSC: NASTEPNY EKRAN");
  } else if (int32_t(noticeUntil - now) > 0)
    label(0, 125, ST77XX_YELLOW, notice);
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
  events = xQueueCreate(64, sizeof(Event));
  binStart = lastFrame = rateStart = millis();
  draw(millis());
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
      if (e.kind == Kind::Sample) {
        latest = e.value;
        lastSample = e.ms;
        haveSample = true;
        ++sampleCount;
        workerState = "BRAK DANYCH";
        if (!paused) {
          history.add(e.value);
          // A gap in reports would smear the spectrum: start a new window.
          if (window.size() && uint32_t(e.ms - window.newestMs()) > fftGapMs)
            window.clear();
          window.add(e.value, e.ms);
        }
      } else {
        history.discardPending();
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



