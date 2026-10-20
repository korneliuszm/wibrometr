#pragma once
#include <cstdint>

// Counts back-and-forth hand strokes along one accelerometer axis. A stroke is
// one full cycle: the high-passed signal swings below -threshold, then above
// +threshold. The slow high-pass removes gravity and tilt of the hand; the
// threshold follows the recent peak so gentle and hard strokes both count.
class StrokeCounter {
  static constexpr float tauS = 1.0f;         // High-pass time constant.
  static constexpr float envDecayS = 3.0f;    // Peak envelope decay time.
  static constexpr float minThreshold = 0.5f; // m/s2, rejects sensor noise.
  static constexpr float fraction = 0.4f;     // Threshold as share of the peak.
  static constexpr float minPeriodS = 0.15f;
  static constexpr float maxGapS = 0.1f;      // Longer sample gaps restart the filter.
  enum Phase : uint8_t { Unknown, Low, High };

  float lowpass_ = 0, hp_ = 0, envelope_ = 0, periodS_ = 0;
  uint32_t lastUs_ = 0, lastStrokeUs_ = 0, count_ = 0;
  Phase phase_ = Unknown;
  bool primed_ = false;

  static constexpr float absf(float v) { return v < 0 ? -v : v; }

public:
  // Call after a gap or reconnect: the next sample only primes the filter.
  constexpr void breakSignal() { primed_ = false; phase_ = Unknown; }
  constexpr void reset() { *this = StrokeCounter{}; }

  constexpr void add(float x, uint32_t us) {
    float dt = 0;
    if (primed_) {
      dt = (us - lastUs_) * 1e-6f;
      if (dt <= 0 || dt > maxGapS) primed_ = false;
    }
    lastUs_ = us;
    if (!primed_) {
      lowpass_ = x;
      hp_ = 0;
      phase_ = Unknown;
      primed_ = true;
      return;
    }
    lowpass_ += dt / (tauS + dt) * (x - lowpass_);
    hp_ = x - lowpass_;
    envelope_ -= envelope_ * dt / envDecayS;
    if (absf(hp_) > envelope_) envelope_ = absf(hp_);

    const float limit = threshold();
    if (hp_ > limit) {
      if (phase_ == Low) stroke(us);
      phase_ = High;
    } else if (hp_ < -limit) {
      phase_ = Low;
    }
  }

  constexpr uint32_t count() const { return count_; }
  constexpr float filtered() const { return hp_; }
  constexpr float envelope() const { return envelope_; }
  constexpr float threshold() const {
    const float t = fraction * envelope_;
    return t > minThreshold ? t : minThreshold;
  }
  // Strokes per minute from recent periods; 0 once strokes have stopped.
  constexpr float perMinute() const {
    if (periodS_ <= 0) return 0;
    const float since = (lastUs_ - lastStrokeUs_) * 1e-6f;
    return since > 2.5f * periodS_ + 1.0f ? 0 : 60.0f / periodS_;
  }

private:
  constexpr void stroke(uint32_t us) {
    const float period = (us - lastStrokeUs_) * 1e-6f;
    if (count_ > 0) {
      if (period < minPeriodS) return;
      periodS_ = (periodS_ <= 0 || period > 3.0f) ? period : 0.7f * periodS_ + 0.3f * period;
    }
    ++count_;
    lastStrokeUs_ = us;
  }
};
