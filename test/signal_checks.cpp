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
