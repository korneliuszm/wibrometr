#pragma once
#include <cstdint>

#include "TouchGestures.h"

// Range-finder settings screen logic without hardware: values, step sizes,
// layout, hit testing and press-and-hold auto-repeat.
namespace rangeui {

enum class Param : uint8_t { Distance, Record, PreRoll, Mode };
constexpr int kParams = 4;

struct Settings {
  int distanceMm = 300;  // Trigger distance.
  int recordS = 5;       // Recording time after the trigger.
  int preRollMs = 1000;  // Part of the measurement before the trigger.
  int mode = 1;          // 0 fast (20 ms), 1 normal (33 ms), 2 precise (200 ms).
};

constexpr int kMinDistanceMm = 30, kMaxDistanceMm = 1200;
constexpr int kMinRecordS = 1, kMaxRecordS = 30;
constexpr int kMaxPreRollMs = 1000;

constexpr int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr Settings clamp(Settings s) {
  s.distanceMm = clampInt(s.distanceMm, kMinDistanceMm, kMaxDistanceMm);
  s.recordS = clampInt(s.recordS, kMinRecordS, kMaxRecordS);
  s.preRollMs = clampInt(s.preRollMs, 0, kMaxPreRollMs);
  s.mode = clampInt(s.mode, 0, 2);
  return s;
}

// One step of a parameter (dir -1 / +1). The distance steps by 10 mm up to 500 mm,
// by 50 mm above; going down, 500 belongs to the lower range (500 down -> 490).
constexpr Settings step(Settings s, Param p, int dir) {
  const int d = dir < 0 ? -1 : 1;
  switch (p) {
  case Param::Distance: {
    const int v = s.distanceMm;
    const int size = d > 0 ? (v < 500 ? 10 : 50) : (v <= 500 ? 10 : 50);
    s.distanceMm = v + d * size;
    break;
  }
  case Param::Record: s.recordS += d; break;
  case Param::PreRoll: s.preRollMs += d * 100; break;
  case Param::Mode: s.mode += d; break;
  }
  return clamp(s);
}

// VL53L0X measurement timing budget of a mode.
constexpr uint32_t budgetUs(int mode) { return mode <= 0 ? 20000 : (mode == 1 ? 33000 : 200000); }
constexpr const char *modeName(int mode) { return mode <= 0 ? "SZYBKI" : (mode == 1 ? "NORMALNY" : "DOKLADNY"); }

// Smallest of {5,10,20,40,80} m/s^2 that holds `peak` (chart scale).
constexpr float chartRangeMs2(float peak) {
  if (peak <= 5.0f) return 5.0f;
  if (peak <= 10.0f) return 10.0f;
  if (peak <= 20.0f) return 20.0f;
  if (peak <= 40.0f) return 40.0f;
  return 80.0f;
}

// Four rows of the settings page, each: [ - ]  label / value  [ + ].
constexpr int kRowY = 38, kRowPitch = 40, kRowH = 38;
constexpr Rect rowRect(Param p) { return {0, int16_t(kRowY + kRowPitch * int(p)), 320, kRowH}; }
constexpr Rect minusRect(Param p) { return {4, rowRect(p).y, 56, kRowH}; }
constexpr Rect plusRect(Param p) { return {260, rowRect(p).y, 56, kRowH}; }

struct Press {
  Param param = Param::Distance;
  int dir = 0;  // -1 minus, +1 plus, 0 nothing pressed.
};

constexpr Press hit(int x, int y) {
  for (int i = 0; i < kParams; ++i) {
    const Param p = Param(i);
    if (minusRect(p).contains(x, y)) return {p, -1};
    if (plusRect(p).contains(x, y)) return {p, +1};
  }
  return {};
}

// Call at any rate with the currently pressed button. Returns -1/0/+1: a step
// right at the press (also when switching buttons or rows), then after 500 ms of
// holding one step every 100 ms (timed from the press), at most one per call.
// Releasing resets it.
class HoldRepeater {
public:
  static constexpr uint32_t kFirstMs = 500;
  static constexpr uint32_t kRepeatMs = 100;

  int update(Press pressed, uint32_t nowMs) {
    if (pressed.dir == 0) {
      held_ = Press{};
      return 0;
    }
    if (pressed.dir != held_.dir || pressed.param != held_.param) {
      held_ = pressed;
      nextMs_ = nowMs + kFirstMs;
      return pressed.dir;
    }
    if (int32_t(nowMs - nextMs_) < 0) return 0;
    do nextMs_ += kRepeatMs; while (int32_t(nowMs - nextMs_) >= 0);
    return pressed.dir;
  }

private:
  Press held_;
  uint32_t nextMs_ = 0;
};

}  // namespace rangeui
