#pragma once
#include <cstdint>
#include "SignalHistory.h"

// MPU6050 accelerometer data decoding; pure arithmetic, tested on a PC.
namespace mpu6050 {

constexpr float kGravity = 9.80665f; // m/s2 per g.

// ACCEL_CONFIG AFS_SEL: 0 = +-2 g, 1 = +-4 g, 2 = +-8 g, 3 = +-16 g.
constexpr float lsbPerG(uint8_t range) { return 16384.0f / float(1 << (range & 3)); }

// Registers are big endian two's complement.
constexpr int16_t be16(uint8_t hi, uint8_t lo) {
  const int v = (int(hi) << 8) | lo;
  return int16_t(v >= 32768 ? v - 65536 : v);
}

// ACCEL_XOUT_H..ACCEL_ZOUT_L (6 bytes) to m/s2, in the chip's own axes.
constexpr Accel decode(const uint8_t (&raw)[6], uint8_t range) {
  const float scale = kGravity / lsbPerG(range);
  return {be16(raw[0], raw[1]) * scale, be16(raw[2], raw[3]) * scale, be16(raw[4], raw[5]) * scale};
}

} // namespace mpu6050
