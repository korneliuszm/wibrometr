#pragma once
#include <cmath>
#include <cstdint>
#include "SignalHistory.h"

// Displacement of the sensor in each axis over one move, from the
// accelerometer (gravity included) and the gyro, using zero-velocity updates:
// - at rest, gravity plus the accelerometer's zero offset is the mean of the
//   last `restSamples` samples, and the gyro bias is the mean of its rates;
// - a move is everything between two rests. The gyro (minus its bias) tracks
//   the orientation from the start of the move, and each acceleration sample
//   is rotated into the sensor axes at that start, so turning the sensor does
//   not move gravity into another axis;
// - in those axes gravity is interpolated from the rest before to the rest
//   after (what the gyro could not explain, e.g. its drift), subtracted and
//   integrated to velocity;
// - velocity is zero at both rests, so the velocity left at the end is drift.
//   It is removed as a linear ramp before the second integral.
// Gravity after the move that does not match the rotated gravity from before
// it means the orientation is wrong (`tilted`). Feeding zero rates (no gyro)
// gives the plain-accelerometer version, where any rotation is such an error.
class DisplacementMeter {
public:
  static constexpr int restSamples = 60;      // 0.3 s at 200 Hz.
  static constexpr float restRange = 0.35f;   // m/s2, max - min per axis at rest.
  static constexpr float gyroRange = 0.05f;   // rad/s (2.9 deg/s), max - min per axis at rest.
  static constexpr float gyroTol = 0.02f;     // rad/s from the bias still counts as rest.
  static constexpr int maxSamples = 3000;     // 15 s at 200 Hz.
  static constexpr int trackPoints = 200;     // Stored trajectory for the plots.
  static constexpr float tiltLimit = 0.6f;    // m/s2 gravity mismatch, about 3.5 deg.
  static constexpr float maxGapS = 0.05f;     // Longer sample gaps abort a move.
  static constexpr float meanTol = 0.15f;     // m/s2 from gravity still counts as rest.
  static constexpr int recentSamples = 20;    // 0.1 s mean for a slow start.
  static constexpr int tiltedRestSamples = 200; // 1 s quiet off the expected gravity.
  enum class State : uint8_t { Settling, Rest, Moving, TooLong };

  struct Move {
    float d[3] = {};            // Net displacement in the sensor axes at the start, m.
    float durationS = 0;        // Rest to rest, including both rest windows.
    float peakSpeed = 0;        // m/s, after drift removal.
    float rotationDeg = 0;      // Total turn of the sensor from the gyro.
    bool tilted = false;
    int points = 0;             // Valid entries of track.
    float track[trackPoints][3] = {}; // Position over time, evenly spaced, m.
  };

  void add(Accel a, uint32_t us) { add(a, Accel{}, us); }
  // a in m/s2, w (angular rate) in rad/s, both in the sensor axes.
  void add(Accel a, Accel w, uint32_t us) {
    float dt = 0;
    if (primed_) {
      dt = (us - lastUs_) * 1e-6f;
      if (dt <= 0 || dt > maxGapS) breakSignal();
    }
    lastUs_ = us;
    primed_ = true;
    ring_[ringHead_] = {a, w, dt};
    ringHead_ = (ringHead_ + 1) % restSamples;
    if (ringCount_ < restSamples) ++ringCount_;
    if (state_ == State::Moving) {
      if (count_ == maxSamples) { state_ = State::TooLong; count_ = 0; return; }
      push(a, w, dt);
    }
    if (ringCount_ < restSamples) return;
    const bool quiet = ringStill();
    switch (state_) {
      case State::Settling:
      case State::TooLong:
        if (quiet) {
          setRest({ringMean(&Sample::a), ringMean(&Sample::w)});
          state_ = State::Rest;
        }
        break;
      case State::Rest:
        // A slow start keeps the window quiet but pulls the newest samples
        // away from gravity (or the rates away from the bias).
        if (quiet && distance(recentMean(&Sample::a), rest_.gravity) < meanTol &&
            distance(recentMean(&Sample::w), rest_.bias) < gyroTol) {
          rest_ = {ringMean(&Sample::a), ringMean(&Sample::w)};
          restHist_[ringHead_] = rest_;
          break;
        }
        // The window before the trigger is the pre-roll: a gentle start of the
        // move is already in it, and so already in the latest rest estimates.
        // Use the ones from before the pre-roll.
        rest_ = restHist_[ringHead_];
        state_ = State::Moving;
        count_ = 0;
        quietRun_ = endWait_ = 0;
        live_ = {};
        liveV_ = {};
        q_ = {1, 0, 0, 0};
        for (int i = 0; i < restSamples; ++i) {
          const Sample &s = ring_[(ringHead_ + i) % restSamples];
          push(s.a, s.w, s.dt);
        }
        break;
      case State::Moving:
        // Near-constant acceleration (the peak of a smooth move) is quiet too,
        // so a rest must also show gravity where the gyro says it should be,
        // or stay quiet long enough that the orientation is most likely off.
        // Once it looks like a rest, wait one more window so the gravity
        // measured after the move holds none of its end.
        quietRun_ = quiet ? quietRun_ + 1 : 0;
        if (!quiet) endWait_ = 0;
        else if (endWait_ == 0) {
          const Accel expected = rotate(conj(q_), rest_.gravity);
          const bool settled = distance(ringMean(&Sample::w), rest_.bias) < gyroTol &&
                               distance(ringMean(&Sample::a), expected) < meanTol;
          if (settled || quietRun_ >= tiltedRestSamples) endWait_ = 1;
        } else if (++endWait_ > restSamples) {
          finish(ringMean(&Sample::a));
          setRest({ringMean(&Sample::a), ringMean(&Sample::w)});
          state_ = State::Rest;
        }
        break;
    }
  }

  // After a gap or reconnect: drop the move in progress and wait for rest.
  void breakSignal() {
    primed_ = false;
    ringCount_ = ringHead_ = 0;
    count_ = 0;
    if (state_ != State::TooLong) state_ = State::Settling;
  }
  void resetTotal() { total_ = {}; last_ = Move{}; moves_ = 0; }

  State state() const { return state_; }
  uint32_t moves() const { return moves_; }
  const Move &last() const { return last_; }
  float total(int i) const { return get(total_, i); }
  // Running estimate during a move (gravity from the rest before, no drift fix).
  float live(int i) const { return get(live_, i); }
  float movingS() const { return state_ == State::Moving ? elapsed_ : 0; }
  // Gyro zero-rate offset from the last rest, rad/s; zero before the first rest.
  Accel gyroBias() const { return rest_.bias; }
  bool biasKnown() const { return restSeen_; }
  bool atRest() const { return state_ == State::Rest; }

private:
  struct Sample { Accel a, w; float dt; };
  struct Rest { Accel gravity, bias; };
  struct Quat { float w, x, y, z; };
  Sample ring_[restSamples] = {};
  Rest restHist_[restSamples] = {}; // Rest estimates at each ring slot's time.
  int ringHead_ = 0, ringCount_ = 0;
  Accel buf_[maxSamples] = {};     // Acceleration in the axes at the start of the move.
  float bufDt_[maxSamples] = {};
  int count_ = 0, quietRun_ = 0, endWait_ = 0;
  float elapsed_ = 0;
  Rest rest_{};
  Quat q_{1, 0, 0, 0};             // Sensor now -> sensor at the start of the move.
  Accel prevW_{}, live_{}, liveV_{}, total_{};
  Move last_{};
  uint32_t lastUs_ = 0, moves_ = 0;
  bool primed_ = false, restSeen_ = false;
  State state_ = State::Settling;

  static float get(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }
  static float &ref(Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

  static Quat conj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
  static Quat mul(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
  }
  static Accel rotate(Quat q, Accel v) {
    const Quat r = mul(mul(q, {0, v.x, v.y, v.z}), conj(q));
    return {r.x, r.y, r.z};
  }

  void setRest(Rest r) {
    rest_ = r;
    restSeen_ = true;
    for (Rest &h : restHist_) h = r;
  }
  bool ringStill() const {
    for (int i = 0; i < 6; ++i) {
      const auto pick = [i](const Sample &s) { return i < 3 ? get(s.a, i) : get(s.w, i - 3); };
      float lo = pick(ring_[0]), hi = lo;
      for (int k = 1; k < restSamples; ++k) {
        const float v = pick(ring_[k]);
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
      }
      if (hi - lo > (i < 3 ? restRange : gyroRange)) return false;
    }
    return true;
  }
  Accel ringMean(Accel Sample::*field) const {
    Accel m{};
    for (const Sample &s : ring_) { m.x += (s.*field).x; m.y += (s.*field).y; m.z += (s.*field).z; }
    return {m.x / restSamples, m.y / restSamples, m.z / restSamples};
  }
  Accel recentMean(Accel Sample::*field) const {
    Accel m{};
    for (int k = 1; k <= recentSamples; ++k) {
      const Accel &s = ring_[(ringHead_ + restSamples - k) % restSamples].*field;
      m.x += s.x; m.y += s.y; m.z += s.z;
    }
    return {m.x / recentSamples, m.y / recentSamples, m.z / recentSamples};
  }
  static float distance(const Accel &a, const Accel &b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  void push(Accel a, Accel w, float dt) {
    if (count_ == 0) elapsed_ = 0, dt = 0;
    elapsed_ += dt;
    if (count_) {
      // Turn over this step: mean of both rates (trapezoid), minus the bias.
      const float rx = 0.5f * (w.x + prevW_.x) - rest_.bias.x, ry = 0.5f * (w.y + prevW_.y) - rest_.bias.y,
                  rz = 0.5f * (w.z + prevW_.z) - rest_.bias.z;
      const float angle = std::sqrt(rx * rx + ry * ry + rz * rz) * dt;
      if (angle > 1e-9f) {
        const float s = std::sin(angle / 2) / (angle / dt);
        q_ = mul(q_, {std::cos(angle / 2), rx * s, ry * s, rz * s});
        const float n = std::sqrt(q_.w * q_.w + q_.x * q_.x + q_.y * q_.y + q_.z * q_.z);
        q_ = {q_.w / n, q_.x / n, q_.y / n, q_.z / n};
      }
    }
    prevW_ = w;
    const Accel r = rotate(q_, a);
    if (count_) {
      for (int i = 0; i < 3; ++i) {
        const float acc = 0.5f * (get(r, i) + get(buf_[count_ - 1], i)) - get(rest_.gravity, i);
        ref(liveV_, i) += acc * dt;
        ref(live_, i) += get(liveV_, i) * dt;
      }
    }
    buf_[count_] = r;
    bufDt_[count_++] = dt;
  }

  void finish(Accel after) {
    const int n = count_;
    const float T = elapsed_;
    count_ = 0;
    if (n < 2 || T <= 0) return;
    Move m;
    m.durationS = T;
    m.rotationDeg = 2 * std::acos(std::fmin(1.0f, std::fabs(q_.w))) * 57.29578f;
    const Accel g0 = rest_.gravity, g1 = rotate(q_, after); // Both in the start axes.
    m.tilted = distance(g0, g1) > tiltLimit;
    const auto acc = [&](int k, int i, float f) {
      return get(buf_[k], i) - (get(g0, i) + f * (get(g1, i) - get(g0, i)));
    };
    // Pass 1: velocity at the end, which should be zero.
    float vEnd[3] = {}, t = 0;
    float prev[3];
    for (int i = 0; i < 3; ++i) prev[i] = acc(0, i, 0);
    for (int k = 1; k < n; ++k) {
      t += bufDt_[k];
      for (int i = 0; i < 3; ++i) {
        const float a = acc(k, i, t / T);
        vEnd[i] += 0.5f * (a + prev[i]) * bufDt_[k];
        prev[i] = a;
      }
    }
    // Pass 2: drift-free velocity integrated to position, sampled for the plots.
    float v[3] = {}, p[3] = {}, vPrev[3] = {};
    for (int i = 0; i < 3; ++i) prev[i] = acc(0, i, 0);
    t = 0;
    const int stride = (n + trackPoints - 2) / (trackPoints - 1);
    int points = 1; // track[0] is the origin.
    for (int k = 1; k < n; ++k) {
      const float dt = bufDt_[k];
      t += dt;
      const float f = t / T;
      float speed2 = 0;
      for (int i = 0; i < 3; ++i) {
        const float a = acc(k, i, f);
        v[i] += 0.5f * (a + prev[i]) * dt;
        prev[i] = a;
        const float vc = v[i] - vEnd[i] * f;
        p[i] += 0.5f * (vc + vPrev[i]) * dt;
        vPrev[i] = vc;
        speed2 += vc * vc;
      }
      if (speed2 > m.peakSpeed * m.peakSpeed) m.peakSpeed = std::sqrt(speed2);
      if ((k % stride == 0 || k == n - 1) && points < trackPoints) {
        for (int i = 0; i < 3; ++i) m.track[points][i] = p[i];
        ++points;
      }
    }
    m.points = points;
    for (int i = 0; i < 3; ++i) { m.d[i] = p[i]; ref(total_, i) += p[i]; }
    last_ = m;
    ++moves_;
  }
};
