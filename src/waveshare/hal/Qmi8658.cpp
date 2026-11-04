#include "hal/Qmi8658.h"
#include <Arduino.h>
#include <cstdio>
#include "Qmi8658Decode.h"
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"

namespace hal {

namespace {
constexpr uint8_t kWhoAmI = 0x00, kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl3 = 0x04, kCtrl5 = 0x06,
                  kCtrl7 = 0x08, kStatus0 = 0x2E, kAxL = 0x35, kReset = 0x60;
constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kSoftReset = 0xB0;
constexpr uint8_t kCtrl1AutoIncrement = 0x40; // ADDR_AI: multi-byte reads walk the registers.
constexpr uint8_t kAccelEnable = 0x01;        // CTRL7: accelerometer on, gyroscope off.
constexpr uint8_t kAccelDataReady = 0x01;     // STATUS0 bit 0.
} // namespace

bool Qmi8658::begin(const Config &config) {
  config_ = config;
  auto fail = [this](const char *text) {
    std::snprintf(error_, sizeof error_, "%s", text);
    return false;
  };
  const uint8_t address = board::kImuAddress;
  if (!I2cBus::probe(address)) return fail("brak odpowiedzi 0x6B");
  uint8_t id = 0;
  if (!I2cBus::read(address, kWhoAmI, &id, 1)) return fail("odczyt WHO_AM_I nieudany");
  whoAmI_ = id;
  if (id != kWhoAmIValue) {
    std::snprintf(error_, sizeof error_, "WHO_AM_I=0x%02X, oczekiwano 0x05", id);
    return false;
  }
  I2cBus::writeByte(address, kReset, kSoftReset);
  delay(30);
  // The chip answers again once the reset is done.
  for (int i = 0; i < 10 && !I2cBus::read(address, kWhoAmI, &id, 1); ++i) delay(10);
  bool ok = I2cBus::writeByte(address, kCtrl1, kCtrl1AutoIncrement);
  ok = ok && I2cBus::writeByte(address, kCtrl2, uint8_t((config.range & 3) << 4) | qmi8658::odrCode(config.odrHz));
  ok = ok && I2cBus::writeByte(address, kCtrl3, 0x00); // Gyroscope range/ODR: unused.
  ok = ok && I2cBus::writeByte(address, kCtrl5, config.lowPass ? uint8_t(((config.lowPassMode & 3) << 1) | 1) : 0);
  ok = ok && I2cBus::writeByte(address, kCtrl7, kAccelEnable);
  if (!ok) return fail("zapis konfiguracji nieudany");
  uint8_t back[2] = {};
  const uint8_t expected = uint8_t((config.range & 3) << 4) | qmi8658::odrCode(config.odrHz);
  if (!I2cBus::read(address, kCtrl2, &back[0], 1) || !I2cBus::read(address, kCtrl7, &back[1], 1) ||
      back[0] != expected || (back[1] & 0x03) != kAccelEnable) {
    std::snprintf(error_, sizeof error_, "CTRL2 0x%02X/0x%02X CTRL7 0x%02X", expected, back[0], back[1]);
    return false;
  }
  delay(30); // First sample after enabling.
  error_[0] = '\0';
  return true;
}

Qmi8658::Poll Qmi8658::poll(Accel &out) {
  uint8_t status = 0;
  if (!I2cBus::read(board::kImuAddress, kStatus0, &status, 1)) return Poll::Error;
  if (!(status & kAccelDataReady)) return Poll::NoData;
  uint8_t raw[6];
  if (!I2cBus::read(board::kImuAddress, kAxL, raw, sizeof raw)) return Poll::Error;
  out = qmi8658::decode(raw, config_.range);
  return Poll::Sample;
}

float Qmi8658::sampleRateHz() const { return qmi8658::odrHz(qmi8658::odrCode(config_.odrHz)); }
float Qmi8658::rangeG() const { return qmi8658::rangeG(config_.range); }

} // namespace hal
