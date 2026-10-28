#include "SignalHistory.h"

constexpr bool peaksAndGaps() {
  SignalHistory<3> history;
  history.add({1, -2, 3});
  history.add({8, -9, 1});
  history.tick();
  auto b = history.at(0);
  if (!b.valid || b.low.x != 1 || b.high.x != 8 ||
      b.low.y != -9 || b.high.z != 3) return false;
  history.tick();
  return history.size() == 2 && !history.at(1).valid;
}
constexpr bool ringOrder() {
  SignalHistory<2> history;
  for (int i = 1; i <= 3; ++i) {
    history.add({float(i), 0, 0});
    history.tick();
  }
  return history.size() == 2 && history.at(0).low.x == 2 &&
         history.at(1).low.x == 3;
}
constexpr bool zeroAndClear() {
  SignalHistory<3> history;
  history.add({4, -2, 9});
  history.tick();
  history.zero({4, -2, 9});
  if (history.size() != 0) return false;
  history.add({6, -5, 10});
  history.tick();
  if (history.at(0).low.x != 2 || history.at(0).low.y != -3 ||
      history.at(0).low.z != 1) return false;
  history.clear();
  history.tick();
  return !history.at(0).valid && history.offset().z == 9;
}
static_assert(peaksAndGaps(), "Peaks must survive and missing data must form gaps");
static_assert(ringOrder(), "Ring must keep newest values in chronological order");
static_assert(zeroAndClear(), "Zero must subtract each axis and clear stale history");

static_assert(elapsedBins(1001, 1000) == 0, "Sub-frame time must not advance");
static_assert(elapsedBins(999, 1000) == 0, "Late queued events must not erase history");
static_assert(elapsedBins(1040, 1000) == 1, "40ms makes one bin");
static_assert(elapsedBins(24, 0xFFFFFFF0u) == 1, "Millis rollover must preserve time");

#include "Spectrum.h"
constexpr bool windowOrderAndRate() {
  SampleWindow<4> window;
  for (int i = 1; i <= 6; ++i) window.add({float(i), 0, 0}, 1000000 + i * 10000);
  return window.full() && window.at(0).x == 3 && window.at(3).x == 6 &&
         window.newestUs() == 1060000 && window.rateHz() == 100.0f;
}
constexpr bool windowClear() {
  SampleWindow<4> window;
  window.add({1, 2, 3}, 5);
  window.clear();
  return window.size() == 0 && window.rateHz() == 0;
}
static_assert(windowOrderAndRate(), "FFT window must keep newest samples and measure rate");
static_assert(windowClear(), "Cleared FFT window must not report a rate");

#include "StrokeCounter.h"
// Full strokes (two minimum-jerk passes each) at 100 Hz along direction
// (dx, dy, dz) on top of an offset. Gravity-free input, as the linear report gives.
constexpr uint32_t strokesCounted(int strokes, float distance, float strokeS, float bias,
                                  float dx = 1, float dy = 0, float dz = 0) {
  StrokeCounter counter;
  const int perPass = int(strokeS / 2 * 100);
  uint32_t us = 0;
  for (int p = 0; p < 2 * strokes; ++p)
    for (int i = 0; i < perPass; ++i) {
      const float t = float(i) / perPass, T = strokeS / 2;
      const float dir = p % 2 ? -1.0f : 1.0f;
      const float a = dir * distance / (T * T) * (60 * t - 180 * t * t + 120 * t * t * t);
      counter.add({a * dx + bias, a * dy - bias, a * dz}, us += 10000);
    }
  return counter.count();
}
constexpr bool near(uint32_t got, uint32_t want) { return got + 1 >= want && got <= want; }
static_assert(near(strokesCounted(10, 0.10f, 0.5f, 0.2f), 10), "2 strokes/s along X must count each stroke once");
static_assert(near(strokesCounted(10, 0.04f, 0.25f, 0.2f), 10), "Fast short strokes must count");
static_assert(near(strokesCounted(10, 0.15f, 1.2f, 0.2f), 10), "Slow long strokes must count");
static_assert(near(strokesCounted(10, 0.10f, 0.5f, 0.2f, 0, 1, 0), 10), "Direction must not matter (Y)");
static_assert(near(strokesCounted(10, 0.10f, 0.5f, 0.2f, 0.5f, 0.5f, 0.7f), 10), "Oblique direction must count");
static_assert(strokesCounted(10, 0.0f, 0.5f, 0.3f) == 0, "Constant offset must not count");
static_assert(strokesCounted(20, 0.0005f, 0.1f, 0.0f) == 0, "10 Hz hand tremor (0.5 mm) must not count");
constexpr bool resetClears() {
  StrokeCounter counter;
  uint32_t us = 0;
  for (int p = 0; p < 6; ++p)
    for (int i = 0; i < 25; ++i) {
      const float t = i / 25.0f;
      counter.add({(p % 2 ? -1 : 1) * 1.6f * (60 * t - 180 * t * t + 120 * t * t * t), 0, 0}, us += 10000);
    }
  const bool counted = counter.count() > 0;
  counter.reset();
  return counted && counter.count() == 0 && counter.perMinute() == 0;
}
static_assert(resetClears(), "Reset must clear the count and rate");

#include "Mpu6050Decode.h"
// MPU6050 decoding: big-endian two's complement, +-8 g = 4096 LSB/g.
constexpr bool mpuDecodes() {
  const uint8_t flat[6] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00}; // Z = +4096 = 1 g.
  const Accel a = mpu6050::decode(flat, 2);
  const uint8_t neg[6] = {0xF0, 0x00, 0xFF, 0xFF, 0x80, 0x00}; // X = -4096, Y = -1, Z = -32768.
  const Accel b = mpu6050::decode(neg, 2);
  const float g = mpu6050::kGravity;
  const auto close = [](float x, float y) { return x - y < 1e-3f && y - x < 1e-3f; };
  return close(a.x, 0) && close(a.y, 0) && close(a.z, g) &&
         close(b.x, -g) && close(b.y, -g / 4096) && close(b.z, -8 * g);
}
static_assert(mpuDecodes(), "MPU6050 samples must decode to m/s2 with the right sign");
static_assert(mpu6050::lsbPerG(0) == 16384.0f && mpu6050::lsbPerG(3) == 2048.0f, "AFS_SEL scales");
constexpr bool gyroDecodes() {
  const uint8_t raw[6] = {0x00, 0x83, 0xFF, 0x7D, 0x00, 0x00}; // X = +131, Y = -131 LSB.
  const Accel w = mpu6050::decodeGyro(raw, 1);                  // +-500 deg/s: 65.5 LSB per deg/s.
  const float want = 2 * mpu6050::kRadPerDeg;
  const auto close = [](float x, float y) { return x - y < 1e-5f && y - x < 1e-5f; };
  return close(w.x, want) && close(w.y, -want) && close(w.z, 0);
}
static_assert(gyroDecodes(), "MPU6050 gyro samples must decode to rad/s with the right sign");
static_assert(mpu6050::lsbPerDps(0) == 131.0f && mpu6050::lsbPerDps(3) == 16.375f, "FS_SEL scales");
static_assert(mpu6050::be16(0x7F, 0xFF) == 32767 && mpu6050::be16(0x80, 0x00) == -32768, "int16 limits");
