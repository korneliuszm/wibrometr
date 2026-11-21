#pragma once
#include <cstdint>

// Settings of the suspension vibration recorder (env waveshare_vibration). The
// sensor is polled every millisecond, so the sample rate must stay at or below
// 500 Hz (two polls per sample).
namespace vib {

constexpr float kSampleRateHz = 500;      // QMI8658 accelerometer ODR.
constexpr uint8_t kRange = 2;             // 0..3 = +-2/4/8/16 g; 2 = +-8 g.
constexpr uint8_t kLowPassMode = 3;       // Hardware low-pass: 13.37 % of the ODR = 67 Hz at 500 Hz.
static_assert(kSampleRateHz <= 500, "polling at 1 kHz needs 500 Hz or less");

constexpr uint32_t kPreRollUs = 1000000;  // Pre-trigger part of each measurement.
constexpr uint32_t kQuietUs = 3000000;    // This long below the threshold ends a measurement.
constexpr uint32_t kWarmupUs = 1000000;   // No trigger after start or a data gap (filter settles).

constexpr uint32_t kFrameMs = 50;         // Screen refresh: 20 frames per second.
constexpr int kBins = 136;                // Chart columns...
constexpr uint32_t kBinMs = 40;           // ...of 40 ms each: a 5.44 s window.

} // namespace vib
