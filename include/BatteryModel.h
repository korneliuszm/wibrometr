#pragma once
#include <cstddef>
#include <cstdint>

// Battery state from a voltage reading and the charger's status line. The
// board has no fuel gauge: the percentage is an estimate from the cell voltage.
// Pure arithmetic, tested on a PC.
namespace battery {

// The ADC sees the cell through a 200 k / 100 k divider (Rev 1 and 2).
constexpr float kDividerRatio = 3.0f;
constexpr float kNoBatteryMv = 2800.0f;   // Below this nothing is connected.
constexpr float kChargeOffsetMv = 80.0f;  // Charging lifts the terminal voltage.

// Typical single-cell Li-ion open-circuit curve, millivolts -> percent.
struct CurvePoint { float mv; float percent; };
constexpr CurvePoint kCurve[] = {
    {3270, 0},  {3610, 5},  {3690, 10}, {3710, 15}, {3730, 20}, {3750, 25}, {3770, 30},
    {3790, 35}, {3800, 40}, {3820, 45}, {3840, 50}, {3850, 55}, {3870, 60}, {3910, 65},
    {3950, 70}, {3980, 75}, {4020, 80}, {4080, 85}, {4110, 90}, {4150, 95}, {4200, 100}};
constexpr std::size_t kCurveSize = sizeof(kCurve) / sizeof(kCurve[0]);

constexpr float percentFromMillivolts(float mv) {
  if (mv <= kCurve[0].mv) return 0;
  if (mv >= kCurve[kCurveSize - 1].mv) return 100;
  for (std::size_t i = 1; i < kCurveSize; ++i)
    if (mv <= kCurve[i].mv)
      return kCurve[i - 1].percent + (kCurve[i].percent - kCurve[i - 1].percent) *
                                         (mv - kCurve[i - 1].mv) / (kCurve[i].mv - kCurve[i - 1].mv);
  return 100;
}

enum class State : uint8_t { NoBattery, OnBattery, Charging, Full };

struct Reading {
  float millivolts = 0; // At the cell terminals.
  float percent = 0;    // Estimate; while charging it is an approximation.
  State state = State::NoBattery;
};

// chargeKnown is false on boards without the charge-detect line (Rev 1); then
// only the voltage is used.
constexpr Reading evaluate(float millivolts, bool charging, bool chargeKnown) {
  Reading r;
  r.millivolts = millivolts;
  if (millivolts < kNoBatteryMv) return r;
  const bool isCharging = chargeKnown && charging;
  r.percent = percentFromMillivolts(isCharging ? millivolts - kChargeOffsetMv : millivolts);
  // Charger finished: the status line is idle while the cell sits at its top.
  r.state = isCharging ? State::Charging
            : (chargeKnown && millivolts >= 4150.0f) ? State::Full
                                                     : State::OnBattery;
  return r;
}

// Median of up to 32 ADC readings in millivolts, to reject switching spikes.
inline float medianMillivolts(float *values, std::size_t n) {
  for (std::size_t i = 1; i < n; ++i) { // Insertion sort: n is tiny.
    const float v = values[i];
    std::size_t j = i;
    for (; j > 0 && values[j - 1] > v; --j) values[j] = values[j - 1];
    values[j] = v;
  }
  return n ? (n % 2 ? values[n / 2] : 0.5f * (values[n / 2 - 1] + values[n / 2])) : 0;
}

} // namespace battery
