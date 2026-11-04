#pragma once
#include <cstddef>
#include <cstdint>

// Touch handling without any hardware: gesture detection, hit testing and page
// navigation. Tested on a PC.

// Raw panel coordinates (portrait 240x320, as the CST816D reports them) to
// screen coordinates for the landscape 320x240 layout. The vendor demo uses
// x = rawY, y = height - 1 - rawX for this orientation.
struct ScreenPoint { int16_t x = 0, y = 0; };
constexpr ScreenPoint mapTouchLandscape(uint16_t rawX, uint16_t rawY, int screenHeight = 240) {
  return {int16_t(rawY), int16_t(screenHeight - 1 - int(rawX))};
}

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  constexpr bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct TouchSample {
  bool down = false;
  int16_t x = 0, y = 0;
};

enum class GestureType : uint8_t { None, Tap, LongPress, SwipeLeft, SwipeRight, SwipeUp, SwipeDown };

struct Gesture {
  GestureType type = GestureType::None;
  int16_t x = 0, y = 0;   // Where the touch started.
  int16_t dx = 0, dy = 0; // Total movement.
  constexpr explicit operator bool() const { return type != GestureType::None; }
};

// Turns a stream of touch samples (call update() at a steady rate, 50-100 Hz)
// into one gesture per touch: a tap or swipe on release, a long press once the
// finger has rested long enough (the release after it is then ignored).
class GestureDetector {
public:
  static constexpr int kTapSlopPx = 14;      // A tap or long press may wobble this much.
  static constexpr int kSwipeMinPx = 45;     // Minimum travel of a swipe.
  static constexpr uint32_t kTapMaxMs = 450;
  static constexpr uint32_t kSwipeMaxMs = 900;
  static constexpr uint32_t kLongPressMs = 700;
  static constexpr int kReleaseSamples = 2;  // Samples without contact that end a touch.

  Gesture update(const TouchSample &s, uint32_t nowMs) {
    if (s.down) {
      upCount_ = 0;
      if (!active_) {
        active_ = true;
        longFired_ = false;
        startMs_ = nowMs;
        startX_ = lastX_ = s.x;
        startY_ = lastY_ = s.y;
        return {};
      }
      lastX_ = s.x;
      lastY_ = s.y;
      if (!longFired_ && nowMs - startMs_ >= kLongPressMs && moved() <= kTapSlopPx) {
        longFired_ = true;
        return make(GestureType::LongPress);
      }
      return {};
    }
    if (!active_) return {};
    if (++upCount_ < kReleaseSamples) return {};
    active_ = false;
    if (longFired_) return {};
    const int dx = lastX_ - startX_, dy = lastY_ - startY_;
    const uint32_t duration = nowMs - startMs_;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx >= kSwipeMinPx && adx * 2 >= ady * 3 && duration <= kSwipeMaxMs)
      return make(dx < 0 ? GestureType::SwipeLeft : GestureType::SwipeRight);
    if (ady >= kSwipeMinPx && ady * 2 >= adx * 3 && duration <= kSwipeMaxMs)
      return make(dy < 0 ? GestureType::SwipeUp : GestureType::SwipeDown);
    if (moved() <= kTapSlopPx && duration <= kTapMaxMs) return make(GestureType::Tap);
    return {};
  }

  bool touching() const { return active_; }

private:
  int moved() const {
    const int dx = lastX_ - startX_, dy = lastY_ - startY_;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    return adx > ady ? adx : ady;
  }
  Gesture make(GestureType t) const {
    Gesture g;
    g.type = t;
    g.x = startX_;
    g.y = startY_;
    g.dx = int16_t(lastX_ - startX_);
    g.dy = int16_t(lastY_ - startY_);
    return g;
  }

  bool active_ = false, longFired_ = false;
  int upCount_ = 0;
  uint32_t startMs_ = 0;
  int16_t startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
};

// Which page is shown; scrolling wraps around.
class PageNavigator {
public:
  explicit constexpr PageNavigator(int count) : count_(count < 1 ? 1 : count) {}
  constexpr int index() const { return index_; }
  constexpr int count() const { return count_; }
  constexpr void next() { index_ = (index_ + 1) % count_; }
  constexpr void prev() { index_ = (index_ + count_ - 1) % count_; }
  constexpr void set(int i) { index_ = i < 0 ? 0 : i >= count_ ? count_ - 1 : i; }

private:
  int count_, index_ = 0;
};

// Bottom bar: [prev] [action 1] [action 2] [next]; swipes work everywhere else.
struct NavBarLayout {
  static constexpr int kScreenW = 320, kScreenH = 240, kBarH = 34;
  static constexpr int kTop = kScreenH - kBarH;
  static constexpr Rect prev{0, kTop, 52, kBarH};
  static constexpr Rect action1{56, kTop, 102, kBarH};
  static constexpr Rect action2{162, kTop, 102, kBarH};
  static constexpr Rect next{268, kTop, 52, kBarH};
  static constexpr bool inBar(int y) { return y >= kTop; }
};

enum class NavButton : uint8_t { None, Prev, Action1, Action2, Next };

// Which bar button, if any, a point falls on.
constexpr NavButton hitNavBar(int x, int y) {
  if (!NavBarLayout::inBar(y)) return NavButton::None;
  if (NavBarLayout::prev.contains(x, y)) return NavButton::Prev;
  if (NavBarLayout::action1.contains(x, y)) return NavButton::Action1;
  if (NavBarLayout::action2.contains(x, y)) return NavButton::Action2;
  if (NavBarLayout::next.contains(x, y)) return NavButton::Next;
  return NavButton::None;
}
