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
// 120 Hz samples, triangle wave of `periodSamples` per cycle around a gravity offset.
constexpr uint32_t strokesInTriangle(int samples, int periodSamples, float amplitude) {
  StrokeCounter counter;
  for (int i = 0; i < samples; ++i) {
    const int phase = i % periodSamples;
    const int tri = phase < periodSamples / 2 ? phase : periodSamples - phase;
    const float x = 5.0f + (tri * 2.0f / periodSamples - 0.5f) * 2 * amplitude;
    counter.add(x, i * 8333u);
  }
  return counter.count();
}
static_assert(strokesInTriangle(600, 60, 3.0f) >= 8 && strokesInTriangle(600, 60, 3.0f) <= 10,
              "2 Hz strokes for 5 s must count about 10 cycles on top of gravity");
static_assert(strokesInTriangle(600, 240, 1.0f) >= 1 && strokesInTriangle(600, 240, 1.0f) <= 2,
              "Slow gentle strokes must still be counted");
static_assert(strokesInTriangle(600, 60, 0.2f) == 0, "Sensor noise must not count as strokes");
constexpr bool resetClears() {
  StrokeCounter counter;
  for (int i = 0; i < 600; ++i) {
    const int phase = i % 60;
    counter.add(5.0f + (phase < 30 ? phase - 15 : 45 - phase) * 0.2f, i * 8333u);
  }
  const bool counted = counter.count() > 0;
  counter.reset();
  return counted && counter.count() == 0 && counter.perMinute() == 0;
}
static_assert(resetClears(), "Reset must clear the count and rate");
