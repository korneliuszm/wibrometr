#pragma once
#include <cstdint>
#include "SignalHistory.h"

// Counts repetitive hand strokes (one stroke = one full back-and-forth cycle)
// from 3-axis gravity-free acceleration, whatever the sensor orientation.
//
// Each axis is smoothed and leaky-integrated into a velocity-like signal. The
// stroke direction is the dominant eigenvector of the recent velocity
// covariance, tracked by one power-iteration step per sample, so it follows
// the grip and wrist rotation. Each pass along that direction makes one
// velocity pulse; two passes make a stroke. Velocity is far cleaner than raw
// acceleration, whose start/stop peaks would double count. The threshold
// follows the recent peak so gentle and hard strokes both count.
class StrokeCounter {
  static constexpr float smoothS = 0.01f;     // Acceleration smoothing (~16 Hz).
  static constexpr float velTauS = 0.6f;      // Leak of the velocity integrator.
  static constexpr float covTauS = 0.5f;      // Memory of the stroke direction.
  static constexpr float envDecayS = 1.0f;    // Peak envelope decay time.
  static constexpr float minThreshold = 0.04f;// m/s, rejects noise and tremor.
  static constexpr float fraction = 0.2f;     // Threshold as share of the peak.
  static constexpr float minPassS = 0.06f;    // Up to ~8 strokes per second.
  static constexpr float maxGapS = 0.1f;      // Longer sample gaps restart the filter.
  enum Phase : uint8_t { Unknown, Low, High };

  float biasS_ = 2.0f; // Time constant of the slow bias (gravity/offset) removal.
  float bias_[3] = {}, acc_[3] = {}, vel_[3] = {};
  float cov_[3][3] = {};
  // Oblique start: an axis orthogonal to the stroke would never move.
  float axis_[3] = {0.57735f, 0.57735f, 0.57735f};
  float proj_ = 0, envelope_ = 0, periodS_ = 0;
  uint32_t lastUs_ = 0, lastPassUs_ = 0, passes_ = 0;
  Phase phase_ = Unknown;
  bool primed_ = false;

  static constexpr float absf(float v) { return v < 0 ? -v : v; }
  static constexpr float sqrtf_(float v) { // constexpr Newton square root
    if (v <= 0) return 0;
    float r = v > 1 ? v : 1;
    for (int i = 0; i < 20; ++i) r = 0.5f * (r + v / r);
    return r;
  }

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

  constexpr void add(Accel a, uint32_t us) {
    const float in[3] = {a.x, a.y, a.z};
    float dt = 0;
    if (primed_) {
      dt = (us - lastUs_) * 1e-6f;
      if (dt <= 0 || dt > maxGapS) primed_ = false;
    }
    lastUs_ = us;
    if (!primed_) {
      for (int i = 0; i < 3; ++i) { bias_[i] = in[i]; acc_[i] = vel_[i] = 0; }
      phase_ = Unknown;
      primed_ = true;
      return;
    }
    for (int i = 0; i < 3; ++i) {
      bias_[i] += dt / (biasS_ + dt) * (in[i] - bias_[i]);
      acc_[i] += dt / (smoothS + dt) * (in[i] - bias_[i] - acc_[i]);
      vel_[i] += acc_[i] * dt - vel_[i] * dt / velTauS;
    }
    // Track the dominant direction; starting from the previous axis keeps its
    // sign continuous, so passes keep alternating in sign.
    const float k = dt / (covTauS + dt);
    float next[3] = {};
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        cov_[i][j] += k * (vel_[i] * vel_[j] - cov_[i][j]);
        next[i] += cov_[i][j] * axis_[j];
      }
    const float norm = sqrtf_(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
    if (norm > 1e-6f)
      for (int i = 0; i < 3; ++i) axis_[i] = next[i] / norm;
    proj_ = vel_[0] * axis_[0] + vel_[1] * axis_[1] + vel_[2] * axis_[2];

    envelope_ -= envelope_ * dt / envDecayS;
    if (absf(proj_) > envelope_) envelope_ = absf(proj_);
    const float limit = threshold();
    if (proj_ > limit) {
      if (phase_ != High) pass(us);
      phase_ = High;
    } else if (proj_ < -limit) {
      if (phase_ != Low) pass(us);
      phase_ = Low;
    }
  }

  // Full strokes (two passes each).
  constexpr uint32_t count() const { return passes_ / 2; }
  constexpr uint32_t passes() const { return passes_; }
  constexpr float filtered() const { return proj_; }
  constexpr float envelope() const { return envelope_; }
  constexpr float axis(int i) const { return axis_[i]; }
  constexpr float threshold() const {
    const float t = fraction * envelope_;
    return t > minThreshold ? t : minThreshold;
  }
  // Strokes per minute from recent pass intervals; 0 once movement has stopped.
  constexpr float perMinute() const {
    if (periodS_ <= 0) return 0;
    const float since = (lastUs_ - lastPassUs_) * 1e-6f;
    return since > 2.5f * periodS_ + 1.0f ? 0 : 30.0f / periodS_;
  }

private:
  constexpr void pass(uint32_t us) {
    const float interval = (us - lastPassUs_) * 1e-6f;
    if (passes_ > 0) {
      if (interval < minPassS) return;
      periodS_ = (periodS_ <= 0 || interval > 3.0f) ? interval : 0.7f * periodS_ + 0.3f * interval;
    }
    ++passes_;
    lastPassUs_ = us;
  }
};
