#pragma once
#include <cstdint>

// Waveshare ESP32-S3-Touch-LCD-2 (SKU 29667). Pins are from the vendor
// schematic GPIO table (Rev 1.0 and Rev 2.0) and agree with the vendor demo.
namespace board {

// 2.0" IPS 240x320, ST7789, SPI. The SD card shares SCLK/MOSI/MISO (own CS).
constexpr int kLcdSclk = 39, kLcdMosi = 38, kLcdMiso = 40;
constexpr int kLcdDc = 42, kLcdCs = 45;
constexpr int kLcdRst = -1;       // No reset pin in use; the driver sends a software reset.
constexpr int kLcdBacklight = 1;  // High = on.
constexpr int kSdCs = 41;

// One I2C bus for the CST816D touch controller and the QMI8658 IMU.
constexpr int kI2cSda = 48, kI2cScl = 47;
constexpr uint32_t kI2cHz = 400000;
constexpr uint8_t kTouchAddress = 0x15;
constexpr uint8_t kImuAddress = 0x6B;
constexpr int kTouchInt = 46;     // Not used: the controller is polled.
constexpr int kImuInt1 = 3;       // Not used: data-ready is on INT2, which is not wired.

// Battery: no fuel gauge. The cell goes through a 200 k / 100 k divider to
// GPIO5 (VBAT = 3 x ADC). The ETA6096 charger's status LED line is turned into
// CHG_DET on GPIO18 on Rev 2.0 boards only: high = charging. On Rev 1.0 GPIO18
// is just a free header pin, so set kHasChargeDetect = false there.
constexpr int kBatteryAdc = 5;
constexpr int kChargeDetect = 18;
constexpr bool kHasChargeDetect = true;

constexpr int kBootButton = 0;

// Landscape orientation: Adafruit_ST7789 rotation 3 sets MADCTL 0x60, the same
// as the vendor library's rotation 1, so the vendor touch mapping applies.
constexpr int kLcdRotation = 3;
constexpr int kScreenW = 320, kScreenH = 240;
constexpr uint32_t kLcdSpiHz = 40000000;
constexpr uint32_t kSdSpiHz = 20000000;

} // namespace board
