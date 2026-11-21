#pragma once
#include <cstdint>

#include "TouchGestures.h"

// Threshold screen logic without hardware: step sizes, layout, hit testing and
// press-and-hold auto-repeat. Thresholds are in deci m/s^2 (20 = 2.0 m/s^2).
namespace thrui {

constexpr int kMinDeci = 2;
constexpr int kMaxDeci = 400;
constexpr int kDefaultDeci = 20;

constexpr int clampDeci(int deci) { return deci < kMinDeci ? kMinDeci : (deci > kMaxDeci ? kMaxDeci : deci); }

// Step 1 below 5.0, 5 from 5.0 to 20.0, 10 above. Going up uses the range of
// `deci`; going down uses it too, but the upper boundary belongs to the lower
// range (50 down -> 49, 200 down -> 195).
constexpr int stepDeci(int deci, int dir) {
  const int d = dir < 0 ? -1 : 1;
  int step = 1;
  if (d > 0) step = deci < 50 ? 1 : (deci < 200 ? 5 : 10);
  else step = deci <= 50 ? 1 : (deci <= 200 ? 5 : 10);
  return clampDeci(deci + d * step);
}

constexpr Rect kMinus{0, 196, 84, 44};
constexpr Rect kPlus{236, 196, 84, 44};
constexpr Rect kChart{26, 30, 294, 146};

enum class Button : uint8_t { None, Minus, Plus };

constexpr Button hit(int x, int y) {
  if (kMinus.contains(x, y)) return Button::Minus;
  if (kPlus.contains(x, y)) return Button::Plus;
  return Button::None;
}

// Smallest of {5,10,20,40,80} m/s^2 that is >= 2x the threshold.
constexpr float chartRangeMs2(int deci) {
  // 2 * deci / 10 <= r  <=>  deci <= 5 * r
  if (deci <= 25) return 5.0f;
  if (deci <= 50) return 10.0f;
  if (deci <= 100) return 20.0f;
  if (deci <= 200) return 40.0f;
  return 80.0f;
}

// Call at any rate with the currently pressed button. Returns -1/0/+1: a step
// right at the press (also when switching buttons), then after 500 ms of
// holding one step every 100 ms (timed from the press), at most one per call.
// Releasing resets it.
class HoldRepeater {
public:
  static constexpr uint32_t kFirstMs = 500;
  static constexpr uint32_t kRepeatMs = 100;

  int update(Button pressed, uint32_t nowMs) {
    if (pressed == Button::None) {
      held_ = Button::None;
      return 0;
    }
    const int dir = pressed == Button::Minus ? -1 : +1;
    if (pressed != held_) {
      held_ = pressed;
      nextMs_ = nowMs + kFirstMs;
      return dir;
    }
    if (int32_t(nowMs - nextMs_) < 0) return 0;
    do nextMs_ += kRepeatMs; while (int32_t(nowMs - nextMs_) >= 0);
    return dir;
  }

private:
  Button held_ = Button::None;
  uint32_t nextMs_ = 0;
};

}  // namespace thrui
