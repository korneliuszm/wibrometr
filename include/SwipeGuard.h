#pragma once
#include <cstdint>

#include "TouchGestures.h"

// Page swipes with a plausibility check on the finger path. The CST816 sometimes
// reports a phantom touch (a point or two, coordinates jumping across the panel,
// e.g. clamped to the edge), which GestureDetector alone takes for a swipe. A
// swipe counts only when the contact lasted a little while and the finger never
// moved faster than a finger can between two samples. Both are measured in time,
// not in samples, so a late sample does not reject a real swipe. No hardware;
// tested on a PC.
class SwipeGuard {
public:
  static constexpr int kSwipeMinPx = 35;        // Shorter than the line app's 45: the first
                                                // tens of ms of a touch are lost (chip waking).
  static constexpr uint32_t kMinContactMs = 60;  // First to last sample with contact. Real quick
                                                // swipes measured from 70 ms; a tap on a button
                                                // whose point drifted 75 px in 70 ms is kept off
                                                // the pages by the caller (buttons act on taps).
  static constexpr int kMaxSpeedPxPerMs = 8;    // A fast real flick: ~3 px/ms.
  static constexpr int kReleaseSamples = 4;     // 40 ms without contact end a touch: the CST816
                                                // drops a finger for a sample or two mid-swipe.

  // A button press counts only after the contact has lasted this long, without a jump
  // (phantom touches of the CST816 last a sample or two, or hop across the panel).
  static constexpr uint32_t kPressMinMs = 60;
  static constexpr bool pressValid(uint32_t contactMs, int maxSpeed) {
    return contactMs >= kPressMinMs && maxSpeed <= kMaxSpeedPxPerMs;
  }
  // A tap (a Tap gesture: short, the finger did not move) counts when it lasted this long without
  // a jump; a phantom touch is a sample or two.
  static constexpr uint32_t kTapMinMs = 30;
  static constexpr bool tapValid(uint32_t contactMs, int maxSpeed) {
    return contactMs >= kTapMinMs && maxSpeed <= kMaxSpeedPxPerMs;
  }

  struct Result {
    Gesture gesture;        // None unless an accepted swipe or another gesture.
    Gesture detected;       // What the detector saw, also when rejected (for the log).
    bool rejected = false;  // A swipe was detected but did not look real.
    uint32_t contactMs = 0;
    int maxSpeed = 0;       // px/ms, rounded up.
  };

  Result update(const TouchSample &s, uint32_t nowMs) {
    if (s.down) {
      if (!det_.touching()) {
        firstMs_ = nowMs;
        maxSpeed_ = 0;
      } else {
        const int dx = s.x - lastX_, dy = s.y - lastY_;
        const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        const int step = adx > ady ? adx : ady;
        const uint32_t dt = nowMs - lastMs_ > 0 ? nowMs - lastMs_ : 1;
        const int speed = int((uint32_t(step) + dt - 1) / dt);
        if (speed > maxSpeed_) maxSpeed_ = speed;
      }
      lastMs_ = nowMs;
      lastX_ = s.x;
      lastY_ = s.y;
    }
    Result r;
    r.gesture = r.detected = det_.update(s, nowMs);
    r.contactMs = lastMs_ - firstMs_;
    r.maxSpeed = maxSpeed_;
    const GestureType t = r.gesture.type;
    const bool swipe = t == GestureType::SwipeLeft || t == GestureType::SwipeRight ||
                       t == GestureType::SwipeUp || t == GestureType::SwipeDown;
    if (swipe && (r.contactMs < kMinContactMs || maxSpeed_ > kMaxSpeedPxPerMs)) {
      r.rejected = true;
      r.gesture = Gesture{};
    }
    return r;
  }

  bool touching() const { return det_.touching(); }

private:
  GestureDetector det_{kSwipeMinPx, kReleaseSamples};
  uint32_t firstMs_ = 0, lastMs_ = 0;
  int maxSpeed_ = 0;
  int16_t lastX_ = 0, lastY_ = 0;
};
