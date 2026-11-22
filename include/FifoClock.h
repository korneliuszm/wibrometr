#pragma once
#include <cmath>
#include <cstdint>

// Timestamps for samples read from the sensor's FIFO in batches.
//
// The sensor produces samples at its own (nominal, not exact) rate; we read a
// batch of n samples every few ms and only know when the batch was read. Each
// sample gets  t = previous + period,  so the time column is uniform. The clock
// follows the read times gently (absorbing scheduling jitter), the period is
// measured over windows of a few seconds (the chip's oscillator is off by a few
// per cent), and a read much later than predicted means the FIFO overflowed:
// the clock re-syncs and reports how many samples were lost.
// Pure arithmetic, tested on a PC.
class FifoClock {
public:
  struct Batch {
    uint32_t firstUs = 0;  // Time of the batch's first sample.
    float firstFrac = 0;   // Sub-microsecond part of firstUs.
    float periodUs = 0;    // Step between the batch's samples.
    uint32_t missed = 0;   // Samples lost before this batch (FIFO overflow).
    uint32_t at(uint32_t i) const { return firstUs + uint32_t(firstFrac + float(i) * periodUs); }
  };

  explicit FifoClock(float nominalHz) : nominalUs_(1e6f / nominalHz), periodUs_(nominalUs_) {}

  void reset() {
    valid_ = false;
    periodUs_ = nominalUs_;
  }
  float rateHz() const { return 1e6f / periodUs_; }
  bool rateMeasured() const { return measured_; }

  // n samples (newest last) read at readUs (taken when the sample count was read).
  Batch batch(uint32_t readUs, uint32_t n) {
    Batch b;
    b.periodUs = periodUs_;
    if (n == 0) {
      b.firstUs = nextUs_;
      b.firstFrac = nextFrac_;
      return b;
    }
    const float span = float(n - 1) * periodUs_;
    if (!valid_) {
      sync(readUs, span);
    } else {
      const uint32_t newest = nextUs_ + uint32_t(nextFrac_ + span);
      const int32_t err = int32_t(readUs - newest);
      const float gap = fmaxf(10000.0f, 8 * periodUs_);
      if (float(err) > gap) {  // Read far later than the FIFO holds: samples were lost.
        b.missed = uint32_t(lroundf((float(err) - periodUs_ / 2) / periodUs_));
        sync(readUs, span);
      } else if (float(err) < -gap) {  // Clock stepped back or the rate was badly off.
        sync(readUs, span);
      } else {
        advance(float(err) / 32);  // Follow the read times slowly.
        count_ += n;
        const uint32_t elapsed = readUs - anchorUs_;
        if (elapsed >= kWindowUs && count_ > 0) {
          float p = float(elapsed) / float(count_);
          p = fminf(fmaxf(p, nominalUs_ * 0.85f), nominalUs_ * 1.15f);
          periodUs_ = measured_ ? periodUs_ + 0.5f * (p - periodUs_) : p;
          measured_ = true;
          anchorUs_ = readUs;
          count_ = 0;
        }
      }
    }
    b.firstUs = nextUs_;
    b.firstFrac = nextFrac_;
    b.periodUs = periodUs_;
    advance(float(n) * periodUs_);
    return b;
  }

private:
  static constexpr uint32_t kWindowUs = 2000000;

  void sync(uint32_t readUs, float span) {
    valid_ = true;
    nextUs_ = readUs;
    nextFrac_ = 0;
    advance(-span);
    anchorUs_ = readUs;
    count_ = 0;
  }
  void advance(float us) {
    const float t = nextFrac_ + us;
    const float whole = std::floor(t);
    nextUs_ += uint32_t(int32_t(whole));
    nextFrac_ = t - whole;
  }

  float nominalUs_, periodUs_;
  bool valid_ = false, measured_ = false;
  uint32_t nextUs_ = 0, anchorUs_ = 0, count_ = 0;
  float nextFrac_ = 0;
};
