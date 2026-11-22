#pragma once
#include <cmath>
#include <cstdint>

// Timestamps for polled samples when the sensor counts its own samples (QMI8658
// TIMESTAMP register), so every lost sample is known exactly.
//
// Each sample is placed (skipped + 1) periods after the previous one, so the time
// column keeps the chip's real spacing even across lost samples. The clock follows
// the read times gently (polling jitter is up to a poll period), and the period is
// measured over 10 s windows: with only the accelerometer on, the chip's ODR comes
// from its internal oscillator and is a few per cent off the nominal value.
// Pure arithmetic, tested on a PC.
class CountedClock {
public:
  explicit CountedClock(float nominalHz) : nominalUs_(1e6f / nominalHz), periodUs_(nominalUs_) {}

  void reset() { valid_ = false; }
  float rateHz() const { return 1e6f / periodUs_; }
  bool rateMeasured() const { return measured_; }

  // A sample read at arrivalUs; `skipped` samples were made before it and lost.
  uint32_t stamp(uint32_t arrivalUs, uint32_t skipped) {
    const uint32_t step = skipped + 1;
    if (!valid_) {
      sync(arrivalUs);
      return tUs_;
    }
    advance(float(step) * periodUs_);
    const int32_t err = int32_t(arrivalUs - tUs_);
    if (err > kResyncUs || err < -kResyncUs) {  // Long stall or a clock step: start over.
      sync(arrivalUs);
      return tUs_;
    }
    advance(float(err) / 64);  // Poll jitter is up to ~1 ms: follow it only slowly.
    count_ += step;
    const uint32_t elapsed = arrivalUs - anchorUs_;
    if (elapsed >= kWindowUs) {
      float p = float(elapsed) / float(count_);
      p = fminf(fmaxf(p, nominalUs_ * 0.85f), nominalUs_ * 1.15f);
      periodUs_ = measured_ ? periodUs_ + 0.5f * (p - periodUs_) : p;
      measured_ = true;
      anchorUs_ = arrivalUs;
      count_ = 0;
    }
    return tUs_;
  }

private:
  static constexpr uint32_t kWindowUs = 10000000;
  static constexpr int32_t kResyncUs = 50000;

  void sync(uint32_t arrivalUs) {
    valid_ = true;
    tUs_ = arrivalUs;
    frac_ = 0;
    anchorUs_ = arrivalUs;
    count_ = 0;
  }
  void advance(float us) {
    const float t = frac_ + us;
    const float whole = std::floor(t);
    tUs_ += uint32_t(int32_t(whole));
    frac_ = t - whole;
  }

  float nominalUs_, periodUs_;
  bool valid_ = false, measured_ = false;
  uint32_t tUs_ = 0, anchorUs_ = 0, count_ = 0;
  float frac_ = 0;
};
