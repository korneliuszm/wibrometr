#pragma once
#include <Wire.h>
#include "SignalHistory.h"

// Minimal MPU6050 driver over I2C (no library): wakes the chip, sets +-8 g,
// +-500 deg/s, a ~94 Hz low-pass filter and a 200 Hz output rate, and raises
// INT (latched, active high) for every new sample; any read clears it. Returns
// acceleration in m/s2 and angular rate in rad/s. Temperature is unused.
class Mpu6050 {
public:
  static constexpr float kSampleRateHz = 200.0f; // Matches kRateDiv in Mpu6050.cpp and LINE_SAMPLE_HZ.
  // Probes 0x68 then 0x69 and configures the first that answers.
  bool begin(TwoWire &wire);
  // Reads the newest sample; false on any I2C error.
  bool read(Accel &accel, Accel &gyro);
  uint8_t address() const { return address_; }
  uint8_t whoAmI() const { return whoAmI_; }

private:
  bool write(uint8_t reg, uint8_t value);
  bool readBytes(uint8_t reg, uint8_t *data, uint8_t length);

  TwoWire *wire_ = nullptr;
  uint8_t address_ = 0, whoAmI_ = 0;
};
