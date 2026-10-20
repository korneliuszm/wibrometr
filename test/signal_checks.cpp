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
// Hand strokes as minimum-jerk passes (alternating direction, no pauses) at
// 120 Hz on top of an offset. Gravity-free input, as the linear report gives.
constexpr uint32_t passesCounted(int passes, float distance, float seconds, float bias) {
  StrokeCounter counter;
  const int perPass = int(seconds * 120);
  uint32_t us = 0;
  for (int p = 0; p < passes; ++p)
    for (int i = 0; i < perPass; ++i) {
      const float t = float(i) / perPass;
      const float dir = p % 2 ? -1.0f : 1.0f;
      const float a = dir * distance / (seconds * seconds) * (60 * t - 180 * t * t + 120 * t * t * t);
      counter.add(a + bias, us += 8333);
    }
  return counter.count();
}
static_assert(passesCounted(10, 0.10f, 0.5f, 0.2f) >= 9 && passesCounted(10, 0.10f, 0.5f, 0.2f) <= 10,
              "Each hand pass must count once");
static_assert(passesCounted(10, 0.03f, 0.8f, 0.2f) >= 9 && passesCounted(10, 0.03f, 0.8f, 0.2f) <= 10,
              "Short gentle strokes must still be counted");
static_assert(passesCounted(10, 0.15f, 0.25f, 0.0f) >= 9 && passesCounted(10, 0.15f, 0.25f, 0.0f) <= 10,
              "Fast strokes must be counted");
static_assert(passesCounted(10, 0.0f, 0.5f, 0.3f) == 0, "Constant offset must not count");
static_assert(passesCounted(10, 0.002f, 0.1f, 0.0f) == 0, "Tremor must not count");
constexpr bool resetClears() {
  StrokeCounter counter;
  uint32_t us = 0;
  for (int p = 0; p < 6; ++p)
    for (int i = 0; i < 60; ++i) {
      const float t = i / 60.0f;
      counter.add((p % 2 ? -1 : 1) * 0.4f * (60 * t - 180 * t * t + 120 * t * t * t), us += 8333);
    }
  const bool counted = counter.count() > 0;
  counter.reset();
  return counted && counter.count() == 0 && counter.cycles() == 0 && counter.perMinute() == 0;
}
static_assert(resetClears(), "Reset must clear the count and rate");
