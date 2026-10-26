#pragma once
#include <cstdint>
#include "IpsConfig.h"

// Smooths noisy position fixes without hiding real movement:
//  - EMA whose step scales with confidence (a 0% fix moves half as much);
//  - a jump larger than maxJumpM is held back until a second fix lands near
//    it, so one bad scan cannot throw the point across the room, but a real
//    move shows up one update later;
//  - the displayed point ignores changes smaller than deadzoneM.
// Pure computation, testable on a PC.
namespace ips {

struct FilterConfig {
  float alpha = kEmaAlpha;
  float maxJumpM = kMaxJumpM;
  int confirmFixes = kJumpConfirmFixes;
  float deadzoneM = kDeadzoneM;
};

class PositionFilter {
public:
  explicit PositionFilter(FilterConfig config = {}) : cfg_(config) {}
  void reset();
  // Feeds one fix; returns false when it was held back as a suspected jump.
  bool update(float xM, float yM, uint8_t confidence);
  bool valid() const { return valid_; }
  float x() const { return shownX_; } // Displayed (dead-zoned) position.
  float y() const { return shownY_; }
  float smoothedX() const { return x_; }
  float smoothedY() const { return y_; }

private:
  FilterConfig cfg_;
  bool valid_ = false;
  float x_ = 0, y_ = 0, shownX_ = 0, shownY_ = 0;
  float pendingX_ = 0, pendingY_ = 0;
  int pending_ = 0;
};

} // namespace ips
