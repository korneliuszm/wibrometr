#pragma once
#include <cstdint>

// Counts hand strokes along one axis from acceleration. Acceleration is
// smoothed and leaky-integrated into a velocity-like signal; every pass of
// the hand makes one velocity pulse, and each pulse in either direction is one
// movement (two movements make one back-and-forth cycle). Velocity is far
// cleaner than raw acceleration, which is jerky and makes the start and stop
// of a stroke look like separate peaks. The threshold follows the recent peak
// so gentle and hard strokes both count.
class StrokeCounter {
  static constexpr float smoothS = 0.02f;     // Acceleration smoothing (~8 Hz).
  static constexpr float velTauS = 0.6f;      // Leak of the velocity integrator.
  static constexpr float envDecayS = 2.0f;    // Peak envelope decay time.
  static constexpr float minThreshold = 0.04f;// m/s, rejects noise and tremor.
  static constexpr float fraction = 0.3f;     // Threshold as share of the peak.
  static constexpr float minPassS = 0.08f;
  static constexpr float maxGapS = 0.1f;      // Longer sample gaps restart the filter.
  enum Phase : uint8_t { Unknown, Low, High };

  float biasS_ = 2.0f; // Time constant of the slow bias (gravity/offset) removal.
  float bias_ = 0, acc_ = 0, vel_ = 0, envelope_ = 0, periodS_ = 0;
  uint32_t lastUs_ = 0, lastPassUs_ = 0, count_ = 0;
  Phase phase_ = Unknown;
  bool primed_ = false;

  static constexpr float absf(float v) { return v < 0 ? -v : v; }

public:
  // Without gravity-free input (plain accelerometer) use a shorter constant.
  constexpr void setBiasTau(float seconds) { biasS_ = seconds; }
  // Call after a gap or reconnect: the next sample only primes the filter.
  constexpr void breakSignal() { primed_ = false; phase_ = Unknown; }
  constexpr void reset() {
    const float tau = biasS_;
    *this = StrokeCounter{};
    biasS_ = tau;
  }

  constexpr void add(float x, uint32_t us) {
    float dt = 0;
    if (primed_) {
      dt = (us - lastUs_) * 1e-6f;
      if (dt <= 0 || dt > maxGapS) primed_ = false;
    }
    lastUs_ = us;
    if (!primed_) {
      bias_ = x;
      acc_ = vel_ = 0;
      phase_ = Unknown;
      primed_ = true;
      return;
    }
    bias_ += dt / (biasS_ + dt) * (x - bias_);
    acc_ += dt / (smoothS + dt) * (x - bias_ - acc_);
    vel_ += acc_ * dt - vel_ * dt / velTauS;
    envelope_ -= envelope_ * dt / envDecayS;
    if (absf(vel_) > envelope_) envelope_ = absf(vel_);

    const float limit = threshold();
    if (vel_ > limit) {
      if (phase_ != High) pass(us);
      phase_ = High;
    } else if (vel_ < -limit) {
      if (phase_ != Low) pass(us);
      phase_ = Low;
    }
  }

  constexpr uint32_t count() const { return count_; }
  constexpr uint32_t cycles() const { return count_ / 2; }
  constexpr float filtered() const { return vel_; }
  constexpr float envelope() const { return envelope_; }
  constexpr float threshold() const {
    const float t = fraction * envelope_;
    return t > minThreshold ? t : minThreshold;
  }
  // Movements per minute from recent intervals; 0 once movement has stopped.
  constexpr float perMinute() const {
    if (periodS_ <= 0) return 0;
    const float since = (lastUs_ - lastPassUs_) * 1e-6f;
    return since > 2.5f * periodS_ + 1.0f ? 0 : 60.0f / periodS_;
  }

private:
  constexpr void pass(uint32_t us) {
    const float interval = (us - lastPassUs_) * 1e-6f;
    if (count_ > 0) {
      if (interval < minPassS) return;
      periodS_ = (periodS_ <= 0 || interval > 3.0f) ? interval : 0.7f * periodS_ + 0.3f * interval;
    }
    ++count_;
    lastPassUs_ = us;
  }
};
