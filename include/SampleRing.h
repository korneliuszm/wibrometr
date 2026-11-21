#pragma once
#include <cstddef>
#include <cstdint>

#include "SignalHistory.h"

// Fixed-size ring of timestamped samples; oldest is overwritten when full.
template <std::size_t N> class SampleRing {
  static_assert(N > 0, "Ring needs storage");
public:
  struct Entry {
    uint32_t tUs = 0;
    Accel a{};
  };
  void push(uint32_t tUs, const Accel &a) {
    buf_[head_] = {tUs, a};
    head_ = (head_ + 1) % N;
    if (count_ < N) ++count_;
  }
  std::size_t size() const { return count_; }
  // i = 0 is the oldest entry.
  Entry at(std::size_t i) const { return buf_[(head_ + N - count_ + i) % N]; }
  void clear() { head_ = count_ = 0; }
private:
  Entry buf_[N]{};
  std::size_t head_ = 0, count_ = 0;
};
