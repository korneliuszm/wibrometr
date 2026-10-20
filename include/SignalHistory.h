#pragma once
#include <cstddef>

struct Accel { float x = 0, y = 0, z = 0; };
struct SignalBin {
  Accel low{}, high{}, first{}, last{};
  bool valid = false;
};
constexpr float lower(float a, float b) { return a < b ? a : b; }
constexpr float upper(float a, float b) { return a > b ? a : b; }

template <std::size_t Capacity> class SignalHistory {
  static_assert(Capacity > 0, "History needs storage");
  SignalBin bins_[Capacity]{}, pending_{};
  Accel offset_{};
  std::size_t head_ = 0, count_ = 0;
public:
  constexpr void add(Accel raw) {
    Accel v{raw.x - offset_.x, raw.y - offset_.y, raw.z - offset_.z};
    if (!pending_.valid) {
      pending_ = {v, v, v, v, true};
    } else {
      pending_.low = {lower(pending_.low.x, v.x), lower(pending_.low.y, v.y),
                      lower(pending_.low.z, v.z)};
      pending_.high = {upper(pending_.high.x, v.x), upper(pending_.high.y, v.y),
                       upper(pending_.high.z, v.z)};
      pending_.last = v;
    }
  }
  constexpr void tick() {
    bins_[head_] = pending_;
    head_ = (head_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    pending_ = {};
  }
  constexpr void clear() { head_ = count_ = 0; pending_ = {}; }
  constexpr void discardPending() { pending_ = {}; }
  constexpr void zero(Accel value) { offset_ = value; clear(); }
  constexpr Accel offset() const { return offset_; }
  constexpr std::size_t size() const { return count_; }
  // Caller supplies an index in [0, size()).
  constexpr SignalBin at(std::size_t index) const {
    return bins_[(head_ + Capacity - count_ + index) % Capacity];
  }
};

#include <cstdint>
constexpr uint32_t elapsedBins(uint32_t now, uint32_t start) {
  const uint32_t delta = now - start;
  return delta > 0x7FFFFFFFu ? 0 : delta / 40;
}
