#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_BNO08x.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_GFX.h>
#include <atomic>
#include <cmath>
#include "SignalHistory.h"
#include "RestartableBNO08x.h"

namespace {
constexpr uint32_t binMs = 40, reportUs = 10000, staleMs = 1000;
constexpr uint32_t retryMs = 3000;
// BNO085 on the shared SPI bus (SCK/MOSI/MISO = GPIO36/35/37).
constexpr uint8_t imuCs = A5, imuInt = A4, imuRst = A3;
constexpr int bins = 100;
constexpr float fullScale = 20.0f;
constexpr int plotX = 30, plotY = 28, plotW = 200, plotH = 79;
constexpr uint16_t bg = 0x0841, grid = 0x2945, muted = 0x9CF3;
constexpr uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, 0x3DFF};
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(240, 135);
SignalHistory<bins> history;
RestartableBNO08x imu(imuRst);
enum class Kind { Sample, Connecting, Waiting, Reset };
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
        // begin_SPI pulses RST before opening SH2, so each retry starts clean.
        if (imu.begin_SPI(imuCs, imuInt, &SPI)) {
          imu.getSensorEvent(&report); // Register the persistent callback target.
          if (imu.enableReport(SH2_ACCELEROMETER, reportUs)) {
            imu.wasReset();
            ready = true;
            lastGood = millis();
            Serial.println("BNO085 SPI: accelerometer requested 100 Hz");
          }
        }
        if (!ready) emit(Kind::Waiting);
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
        ready = false;
        emit(Kind::Waiting);
        retryAt = millis() + retryMs;
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
        history.zero({});
        zeroed = false;
        binStart = now;
        showNotice("PELNY ODCZYT", now);
      } else if (duration >= 1000) {
        if (fresh(now)) {
          history.zero(latest);
          zeroed = true;
          binStart = now;
          showNotice("USTAWIONO ZERO", now);
        } else showNotice("BRAK DANYCH DO ZERA", now);
      } else {
        paused = !paused;
        if (!paused) { history.clear(); binStart = now; }
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
void draw(uint32_t now) {
  canvas.fillScreen(bg);
  label(0, 0, ST77XX_WHITE, "DRGANIA");
  label(56, 0, colors[0], "X");
  label(70, 0, colors[1], "Y");
  label(84, 0, colors[2], "Z");
  label(100, 0, muted, "m/s2");
  canvas.setCursor(162, 0);
  canvas.printf("%3.0f Hz", fresh(now) ? actualHz : 0);
  const char *state = fresh(now) ? (paused ? "PAUZA" : "NA ZYWO") : workerState;
  label(0, 13, fresh(now) ? ST77XX_GREEN : ST77XX_YELLOW, state);
  label(144, 13, muted, zeroed ? "OFFSET" : "PELNY");
  label(0, plotY, muted, "+20");
  label(18, plotY + plotH / 2 - 3, muted, "0");
  label(0, plotY + plotH - 7, muted, "-20");
  for (int n = 0; n <= 4; ++n) {
    int y = plotY + n * (plotH - 1) / 4;
    canvas.drawFastHLine(plotX, y, plotW, grid);
    int x = plotX + n * (plotW - 1) / 4;
    canvas.drawFastVLine(x, plotY, plotH, grid);
  }
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
  const bool holding = buttonStable;
  if (holding) {
    uint32_t held = now - buttonDown;
    label(0, 125, ST77XX_YELLOW, held >= 3000 ? "PUSC: PELNY ODCZYT" :
          held >= 1000 ? "PUSC: ZERO / 3s: PELNY" : "BOOT: PAUZA / 1s: ZERO");
  } else if (int32_t(noticeUntil - now) > 0)
    label(0, 125, ST77XX_YELLOW, notice);
  else label(0, 125, muted, "BOOT: pauza / 1s:zero / 3s:reset");
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
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
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
        if (!paused) history.add(e.value);
      } else {
        history.discardPending();
        haveSample = false;
        workerState = e.kind == Kind::Connecting ? "LACZENIE BNO085" :
                      e.kind == Kind::Reset ? "RESET CZUJNIKA" : "BRAK BNO085 / RETRY";
      }
    }
  }
  uint32_t now = millis();
  advanceTo(now);
  pollButton(now);
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



