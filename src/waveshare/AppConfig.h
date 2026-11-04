#pragma once
#include <cstdint>

// Measurement settings. The sensor is polled every millisecond, so the sample
// rate must stay at or below 500 Hz (two polls per sample).
namespace app {

constexpr float kSampleRateHz = 500;       // QMI8658 accelerometer ODR.
constexpr uint8_t kRange = 2;              // 0..3 = +-2/4/8/16 g; 2 = +-8 g.
constexpr uint8_t kLowPassMode = 3;        // Hardware low-pass: 13.37 % of the ODR = 67 Hz at 500 Hz.
static_assert(kSampleRateHz <= 500, "polling at 1 kHz needs 500 Hz or less");

constexpr uint32_t kFrameMs = 50;          // Screen refresh: 20 frames per second.
constexpr uint32_t kTouchPollMs = 10;
constexpr uint32_t kStaleMs = 1000;        // No sample for this long: sensor shown as lost.
constexpr uint32_t kBatteryLogMs = 10000;  // Battery event interval while recording.

} // namespace app
