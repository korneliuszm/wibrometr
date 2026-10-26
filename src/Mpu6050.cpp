#include "Mpu6050.h"
#include <Arduino.h>
#include "Mpu6050Decode.h"

namespace {
constexpr uint8_t kSmplrtDiv = 0x19, kConfig = 0x1A, kGyroConfig = 0x1B, kAccelConfig = 0x1C;
constexpr uint8_t kIntPinCfg = 0x37, kIntEnable = 0x38;
constexpr uint8_t kAccelXoutH = 0x3B, kPwrMgmt1 = 0x6B, kPwrMgmt2 = 0x6C, kWhoAmI = 0x75;
constexpr uint8_t kRange = 2;     // +-8 g: 4096 LSB/g, headroom for hand movement and knocks.
constexpr uint8_t kDlpf = 2;      // Accelerometer bandwidth 94 Hz; internal rate 1 kHz.
constexpr uint8_t kRateDiv = 4;   // Output rate 1 kHz / (1 + 4) = 200 Hz.
// INT: active high, push-pull, held until any register read (LATCH_INT_EN |
// INT_RD_CLEAR) so a slow task never misses an edge; data-ready interrupt on.
constexpr uint8_t kIntLatchedClearOnRead = 0x30, kDataReadyEnable = 0x01;
} // namespace

bool Mpu6050::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Mpu6050::readBytes(uint8_t reg, uint8_t *data, uint8_t length) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, length) != length) return false;
  for (uint8_t i = 0; i < length; ++i) data[i] = wire_->read();
  return true;
}

bool Mpu6050::begin(TwoWire &wire) {
  wire_ = &wire;
  address_ = 0;
  for (uint8_t candidate : {uint8_t(0x68), uint8_t(0x69)}) {
    wire_->beginTransmission(candidate);
    if (wire_->endTransmission() == 0) { address_ = candidate; break; }
  }
  if (!address_) return false;
  // Reset, then wake with the gyro PLL as clock (more stable than the 8 MHz
  // internal oscillator). A clone that ignores the reset still passes the
  // read-back check below.
  write(kPwrMgmt1, 0x80);
  delay(100);
  if (!write(kPwrMgmt1, 0x01)) return false;
  delay(10);
  whoAmI_ = 0;
  if (!readBytes(kWhoAmI, &whoAmI_, 1)) return false;
  const bool configured = write(kSmplrtDiv, kRateDiv) && write(kConfig, kDlpf) && write(kGyroConfig, 0) &&
                          write(kAccelConfig, uint8_t(kRange << 3)) && write(kPwrMgmt2, 0x07) &&
                          write(kIntPinCfg, kIntLatchedClearOnRead) && write(kIntEnable, kDataReadyEnable);
  uint8_t back = 0xFF;
  return configured && readBytes(kAccelConfig, &back, 1) && back == uint8_t(kRange << 3);
}

bool Mpu6050::read(Accel &out) {
  uint8_t raw[6];
  if (!readBytes(kAccelXoutH, raw, sizeof raw)) return false;
  out = mpu6050::decode(raw, kRange);
  return true;
}
