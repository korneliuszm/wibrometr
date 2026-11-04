#pragma once
#include <cstdint>

// Timestamps for samples that are polled rather than interrupt-driven.
//
// The sensor produces samples at a fixed rate, but we only learn about each one
// when the poll finds it, up to a poll period late and with scheduler jitter.
// Feeding those raw arrival times to the FFT would add timing noise. This clock
// advances by exactly one nominal period per sample and only gently follows the
// arrival times (a phase-locked loop), so the timestamps are uniform while
// slow drift between the sensor's clock and ours is still tracked. A large
// jump means samples were missed: the clock re-syncs and reports how many.
class SampleClock {
public:
  struct Stamp {
    uint32_t us = 0;
    uint32_t missed = 0; // Whole samples lost before this one (0 normally).
  };

  constexpr void reset() { valid_ = false; }

  // arrivalUs: when the sample was observed; periodUs: nominal sample period.
  constexpr Stamp stamp(uint32_t arrivalUs, uint32_t periodUs) {
    Stamp s;
    if (!valid_) {
      valid_ = true;
      lastUs_ = arrivalUs;
      s.us = arrivalUs;
      return s;
    }
    const int32_t error = int32_t(arrivalUs - (lastUs_ + periodUs));
    const int32_t limit = int32_t(periodUs) + int32_t(periodUs) / 2;
    if (error > limit) { // Samples were missed: follow the clock, count the gap.
      s.missed = uint32_t((error + int32_t(periodUs) / 2) / int32_t(periodUs));
      lastUs_ = arrivalUs;
    } else if (error < -limit) { // Clock stepped backwards: re-sync.
      lastUs_ = arrivalUs;
    } else {
      lastUs_ = lastUs_ + periodUs + uint32_t(error / 16);
    }
    s.us = lastUs_;
    return s;
  }

private:
  bool valid_ = false;
  uint32_t lastUs_ = 0;
};
