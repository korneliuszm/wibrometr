#pragma once
#include <cstdint>

#include "RangeTrigger.h"

// VL53L0X result block: 12 bytes from register 0x14 (RESULT_RANGE_STATUS). Byte 0
// holds the range status in bits 6..3 (11 = valid range), bytes 10-11 the range in
// mm, big-endian; 8190 and above mean "nothing in range". No hardware; tested on a PC.
namespace vl53 {

constexpr uint8_t kAddress = 0x29;
constexpr uint8_t kRegInterruptStatus = 0x13;  // Low 3 bits non-zero: a result is ready.
constexpr uint8_t kRegInterruptClear = 0x0B;   // Write 1 after reading the result.
constexpr uint8_t kRegResult = 0x14;
constexpr int kResultBytes = 12;
constexpr uint8_t kStatusRangeValid = 11;
constexpr uint16_t kOutOfRangeMm = 8190;
constexpr uint8_t kRegSoftReset = 0xBF;  // Write 0: device in reset (model id reads 0); write 1: boots.
constexpr uint8_t kRegModelId = 0xC0;    // 0xEE once the device is up.
constexpr uint8_t kModelId = 0xEE;

// Ambient light rate of a result, Mcps (9.7 fixed point at bytes 8-9). A healthy sensor in a room
// reads ~0.3; the same sensor started while it was already ranging read 8-10 and saw nothing.
constexpr float kBlindAmbientMcps = 4.0f;
constexpr float ambientMcps(const uint8_t (&block)[kResultBytes]) {
  return float((block[8] << 8) | block[9]) / 128.0f;
}

constexpr bool resultReady(uint8_t interruptStatus) { return (interruptStatus & 0x07) != 0; }

// Range in mm, or RangeTrigger::kNoReading when the reading is not a valid range.
constexpr uint16_t decodeRange(const uint8_t (&block)[kResultBytes]) {
  const uint8_t status = (block[0] >> 3) & 0x0F;
  const uint16_t mm = uint16_t((block[10] << 8) | block[11]);
  return status == kStatusRangeValid && mm < kOutOfRangeMm ? mm : RangeTrigger::kNoReading;
}

}  // namespace vl53
