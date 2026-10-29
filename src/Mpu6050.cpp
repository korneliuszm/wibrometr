#include "Mpu6050.h"
#include <Arduino.h>
#include "Mpu6050Decode.h"

namespace {
constexpr uint8_t kSmplrtDiv = 0x19, kConfig = 0x1A, kGyroConfig = 0x1B, kAccelConfig = 0x1C;
constexpr uint8_t kIntPinCfg = 0x37, kIntEnable = 0x38;
constexpr uint8_t kAccelXoutH = 0x3B, kPwrMgmt1 = 0x6B, kPwrMgmt2 = 0x6C, kWhoAmI = 0x75;
constexpr uint8_t kRange = 2;     // +-8 g: 4096 LSB/g, headroom for hand movement and knocks.
constexpr uint8_t kGyroRange = 1; // +-500 deg/s: 65.5 LSB/(deg/s), enough for a turning wrist.
constexpr uint8_t kDlpf = 1;      // Bandwidth 184 Hz accel / 188 Hz gyro; internal rate 1 kHz.
constexpr uint8_t kRateDiv = 0;   // Output rate 1 kHz / (1 + 0) = 1 kHz, the most with the filter on.
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
    const uint8_t status = wire_->endTransmission();
    Serial.printf("MPU6050 probe 0x%02X: I2C status=%u\n", candidate, status);
    if (status == 0) { address_ = candidate; break; }
  }
  if (!address_) return false;
  // Reset, then wake with the gyro PLL as clock (more stable than the 8 MHz
  // internal oscillator). A clone that ignores the reset still passes the
  // read-back check below.
  write(kPwrMgmt1, 0x80);
  delay(100);
  if (!write(kPwrMgmt1, 0x01)) {
    Serial.println("MPU6050: wake register write failed");
    return false;
  }
  delay(10);
  whoAmI_ = 0;
  if (!readBytes(kWhoAmI, &whoAmI_, 1)) {
    Serial.println("MPU6050: WHO_AM_I read failed");
    return false;
  }
  Serial.printf("MPU6050: WHO_AM_I=0x%02X\n", whoAmI_);
  const bool configured = write(kSmplrtDiv, kRateDiv) && write(kConfig, kDlpf) &&
                          write(kGyroConfig, uint8_t(kGyroRange << 3)) &&
                          write(kAccelConfig, uint8_t(kRange << 3)) && write(kPwrMgmt2, 0x00) &&
                          write(kIntPinCfg, kIntLatchedClearOnRead) && write(kIntEnable, kDataReadyEnable);
  uint8_t back = 0xFF;
  const bool readBack = configured && readBytes(kAccelConfig, &back, 1);
  Serial.printf("MPU6050 config: writes=%u readback=%u ACCEL_CONFIG=0x%02X expected=0x%02X\n",
                configured, readBack, back, uint8_t(kRange << 3));
  return readBack && back == uint8_t(kRange << 3);
}

// One burst: accel (6 bytes), temperature (2, unused), gyro (6), all from the
// same sample, turned into the mounting axes.
bool Mpu6050::read(Accel &accel, Accel &gyro) {
  uint8_t raw[14];
  if (!readBytes(kAccelXoutH, raw, sizeof raw)) return false;
  const uint8_t a[6] = {raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]};
  accel = mpu6050::mounted(mpu6050::decode(a, kRange));
  gyro = mpu6050::mounted(mpu6050::decodeGyro(raw + 8, kGyroRange));
  return true;
}
