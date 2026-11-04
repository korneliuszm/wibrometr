#pragma once
#include <cstdint>
#include "SignalHistory.h"

// QMI8658 accelerometer: register values and decoding; pure arithmetic, tested on a PC.
namespace qmi8658 {

constexpr float kGravity = 9.80665f; // m/s2 per g.

// CTRL2 aFS (bits 6:4): 0 = +-2 g, 1 = +-4 g, 2 = +-8 g, 3 = +-16 g.
constexpr float lsbPerG(uint8_t range) { return 16384.0f / float(1 << (range & 3)); }
constexpr float rangeG(uint8_t range) { return 2.0f * float(1 << (range & 3)); }

// CTRL2 aODR (bits 3:0), accelerometer-only mode.
constexpr float odrHz(uint8_t code) {
  return code == 0 ? 8000.0f : code == 1 ? 4000.0f : code == 2 ? 2000.0f : code == 3 ? 1000.0f :
         code == 4 ? 500.0f : code == 5 ? 250.0f : code == 6 ? 125.0f : code == 7 ? 62.5f :
         code == 8 ? 31.25f : 0.0f;
}
constexpr uint8_t odrCode(float hz) {
  return hz >= 8000 ? 0 : hz >= 4000 ? 1 : hz >= 2000 ? 2 : hz >= 1000 ? 3 : hz >= 500 ? 4 :
         hz >= 250 ? 5 : hz >= 125 ? 6 : hz >= 62.5f ? 7 : 8;
}

// CTRL5 accelerometer low-pass: bit 0 enable, bits 2:1 mode (share of the ODR).
constexpr float lpfShare(uint8_t mode) {
  return mode == 0 ? 0.0266f : mode == 1 ? 0.0363f : mode == 2 ? 0.0539f : 0.1337f;
}

// Registers are little endian two's complement.
constexpr int16_t le16(uint8_t lo, uint8_t hi) {
  const int v = (int(hi) << 8) | lo;
  return int16_t(v >= 32768 ? v - 65536 : v);
}

// AX_L..AZ_H (6 bytes) to m/s2, in the chip's own axes.
constexpr Accel decode(const uint8_t (&raw)[6], uint8_t range) {
  const float scale = kGravity / lsbPerG(range);
  return {le16(raw[0], raw[1]) * scale, le16(raw[2], raw[3]) * scale, le16(raw[4], raw[5]) * scale};
}

} // namespace qmi8658
