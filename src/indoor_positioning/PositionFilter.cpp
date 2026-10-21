#include "PositionFilter.h"
#include <cmath>

namespace ips {

void PositionFilter::reset() {
  valid_ = false;
  pending_ = 0;
}

bool PositionFilter::update(float xM, float yM, uint8_t confidence) {
  if (!std::isfinite(xM) || !std::isfinite(yM)) return false;
  if (!valid_) {
    x_ = shownX_ = xM;
    y_ = shownY_ = yM;
    valid_ = true;
    pending_ = 0;
    return true;
  }
  float alpha = cfg_.alpha * (0.5f + 0.5f * (confidence > 100 ? 100 : confidence) / 100.0f);
  if (std::hypot(xM - x_, yM - y_) > cfg_.maxJumpM) {
    const bool samePlace = pending_ > 0 && std::hypot(xM - pendingX_, yM - pendingY_) <= cfg_.maxJumpM;
    pending_ = samePlace ? pending_ + 1 : 1;
    pendingX_ = xM;
    pendingY_ = yM;
    if (pending_ < cfg_.confirmFixes) return false;
    // Confirmed move: follow it decisively instead of crawling across.
    alpha = alpha > 0.7f ? alpha : 0.7f;
  }
  pending_ = 0;
  x_ += alpha * (xM - x_);
  y_ += alpha * (yM - y_);
  if (std::hypot(x_ - shownX_, y_ - shownY_) >= cfg_.deadzoneM) {
    shownX_ = x_;
    shownY_ = y_;
  }
  return true;
}

} // namespace ips
