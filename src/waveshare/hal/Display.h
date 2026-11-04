#pragma once
#include <Adafruit_GFX.h>
#include <SPI.h>

namespace hal {

// The SPI bus shared by the LCD and the SD card (SCLK 39, MOSI 38, MISO 40).
SPIClass &spiBus();

// 2.0" 240x320 ST7789 shown in landscape (320x240), drawn from a canvas.
class Display {
public:
  void begin();
  // Pushes the whole canvas. Sent in bands, each its own SPI transaction, so
  // the SD card can use the shared bus between them.
  void flush(GFXcanvas16 &canvas);
  void setBacklight(uint8_t percent);
};

} // namespace hal
