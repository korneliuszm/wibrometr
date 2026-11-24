#pragma once
#include <cstddef>

// Linear correction of the touch position, per axis: corrected = scale * measured +
// offset, both in screen pixels (320x240 landscape). Fitted by least squares from taps on
// known targets. No hardware; tested on a PC.
struct TouchCal {
  float ax = 1, bx = 0, ay = 1, by = 0;
  constexpr float x(float measured) const { return ax * measured + bx; }
  constexpr float y(float measured) const { return ay * measured + by; }
};

namespace touchcal {

struct Point { float x = 0, y = 0; };

// Targets of the on-device calibration: four corners 30 px in, then the centre.
constexpr std::size_t kTargets = 5;
constexpr Point kTarget[kTargets] = {{30, 30}, {290, 30}, {30, 210}, {290, 210}, {160, 120}};

// Least squares of target = a * measured + b on each axis. Needs n >= 2 and some
// spread in the measured values; otherwise returns the identity.
inline TouchCal fit(const Point *target, const Point *measured, std::size_t n) {
  TouchCal cal;
  if (n < 2) return cal;
  const auto axis = [&](auto get, float &a, float &b) {
    double sm = 0, st = 0, smm = 0, smt = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const double m = get(measured[i]), t = get(target[i]);
      sm += m;
      st += t;
      smm += m * m;
      smt += m * t;
    }
    const double den = n * smm - sm * sm;
    if (den < 1.0) return;
    a = float((n * smt - sm * st) / den);
    b = float((st - a * sm) / n);
  };
  axis([](const Point &p) { return p.x; }, cal.ax, cal.bx);
  axis([](const Point &p) { return p.y; }, cal.ay, cal.by);
  return cal;
}

// A plausible correction: a scale near 1 and an offset of at most 60 px. Anything else is a
// mis-tap and is not applied.
constexpr bool plausible(const TouchCal &c) {
  return c.ax > 0.7f && c.ax < 1.3f && c.ay > 0.7f && c.ay < 1.3f && c.bx > -60 && c.bx < 60 &&
         c.by > -60 && c.by < 60;
}

}  // namespace touchcal
