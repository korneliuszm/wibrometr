#pragma once
#include <cstdint>
#include "SignalHistory.h"

// Turns a uniform stream at any rate of at least the output rate into samples
// at exactly 1e6 / periodUs Hz: the mean of all input samples that fall into
// each time bin, stamped with the end of the bin. The line tracker and the
// displacement meter are built for 200 Hz; the QMI8658 runs at 500 Hz so the
// vibration analysis keeps more bandwidth. Averaging over the bin is a crude
// low-pass, which is what the sensor's own filter (67 Hz at 500 Hz) leaves room for.
// A gap in the input skips bins, so consumers see a longer time step and
// restart themselves.
class Decimator {
public:
  struct Out {
    Accel a{}, w{};
    uint32_t us = 0;
  };

  constexpr explicit Decimator(uint32_t periodUs = 5000) : period_(periodUs) {}
  constexpr void reset() { started_ = false; n_ = 0; sa_ = sw_ = {}; }

  // Feeds one sample; true when `out` holds a finished bin.
  constexpr bool add(Accel a, Accel w, uint32_t us, Out &out) {
    bool done = false;
    if (!started_) {
      started_ = true;
      end_ = us + period_;
    } else if (int32_t(us - end_) >= 0) {
      if (n_) {
        out.a = {sa_.x / n_, sa_.y / n_, sa_.z / n_};
        out.w = {sw_.x / n_, sw_.y / n_, sw_.z / n_};
        out.us = end_;
        done = true;
      }
      end_ += (uint32_t(us - end_) / period_ + 1) * period_;
      n_ = 0;
      sa_ = sw_ = {};
    }
    sa_ = {sa_.x + a.x, sa_.y + a.y, sa_.z + a.z};
    sw_ = {sw_.x + w.x, sw_.y + w.y, sw_.z + w.z};
    ++n_;
    return done;
  }

private:
  uint32_t period_;
  uint32_t end_ = 0;
  Accel sa_{}, sw_{};
  int n_ = 0;
  bool started_ = false;
};
