#include "hal/Display.h"
#include <Adafruit_ST7789.h>
#include "hal/BoardPins.h"

namespace hal {

SPIClass &spiBus() {
  static SPIClass bus(FSPI);
  return bus;
}

namespace {
Adafruit_ST7789 &panel() {
  static Adafruit_ST7789 tft(&spiBus(), board::kLcdCs, board::kLcdDc, board::kLcdRst);
  return tft;
}
constexpr uint32_t kBacklightHz = 5000;
constexpr int kBands = 4;
} // namespace

void Display::begin() {
  // Both chip selects high before anything moves on the shared bus.
  pinMode(board::kSdCs, OUTPUT);
  digitalWrite(board::kSdCs, HIGH);
  pinMode(board::kLcdCs, OUTPUT);
  digitalWrite(board::kLcdCs, HIGH);
  spiBus().begin(board::kLcdSclk, board::kLcdMiso, board::kLcdMosi, -1);
  Adafruit_ST7789 &tft = panel();
  tft.init(240, 320);
  tft.setRotation(board::kLcdRotation);
  tft.setSPISpeed(board::kLcdSpiHz);
  tft.fillScreen(0);
  ledcAttach(board::kLcdBacklight, kBacklightHz, 8);
  setBacklight(100);
}

void Display::setBacklight(uint8_t percent) {
  ledcWrite(board::kLcdBacklight, uint32_t(percent > 100 ? 100 : percent) * 255 / 100);
}

void Display::flush(GFXcanvas16 &canvas) {
  Adafruit_ST7789 &tft = panel();
  const uint16_t *pixels = canvas.getBuffer();
  const int w = canvas.width(), h = canvas.height();
  const int rows = h / kBands;
  for (int band = 0; band < kBands; ++band) {
    const int y = band * rows;
    const int count = (band == kBands - 1 ? h - y : rows);
    tft.startWrite();
    tft.setAddrWindow(0, y, w, count);
    tft.writePixels(const_cast<uint16_t *>(pixels) + size_t(y) * w, uint32_t(w) * count);
    tft.endWrite();
  }
}

} // namespace hal
