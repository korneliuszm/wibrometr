#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

// Peaks of the measurements recorded since power-up (the session), for the second
// screen's chart. No hardware access; tested on a PC.

struct PeakEntry {
  unsigned number = 0;   // File number (M<number>.CSV).
  float peak = 0;        // Largest length of the vector XYZ (deviation from the mean), m/s2.
  float axis[3] = {0, 0, 0};  // Largest |deviation| per axis, m/s2.
};

// The newest kCapacity peaks; older ones are dropped (index() keeps counting).
template <std::size_t Capacity> class PeakLog {
  static_assert(Capacity > 0, "Log needs storage");
  PeakEntry entries_[Capacity]{};
  std::size_t head_ = 0, count_ = 0;
  unsigned dropped_ = 0;
public:
  void add(const PeakEntry &e) {
    entries_[head_] = e;
    head_ = (head_ + 1) % Capacity;
    if (count_ < Capacity) ++count_;
    else ++dropped_;
  }
  std::size_t size() const { return count_; }
  // 1-based position in the session of at(0), i.e. 1 until something was dropped.
  unsigned firstIndex() const { return dropped_ + 1; }
  // Caller supplies an index in [0, size()); 0 is the oldest kept.
  const PeakEntry &at(std::size_t i) const { return entries_[(head_ + Capacity - count_ + i) % Capacity]; }
  float maxPeak() const {
    float m = 0;
    for (std::size_t i = 0; i < count_; ++i) m = at(i).peak > m ? at(i).peak : m;
    return m;
  }
};

namespace peakchart {

// Smallest of {1, 2, 2.5, 5, 10} x 10^k that is >= v (v <= 0 gives 1).
inline float niceCeil(float v) {
  if (!(v > 0)) return 1.0f;
  const float mag = std::pow(10.0f, std::floor(std::log10(v)));
  const float m = v / mag;
  const float steps[] = {1.0f, 2.0f, 2.5f, 5.0f, 10.0f};
  for (float s : steps)
    if (m <= s * 1.0001f) return s * mag;
  return 10.0f * mag;
}

// Y range for a chart of peaks: a little headroom above the largest, never below
// `floor` (so a quiet session does not blow noise up to full scale).
inline float yRange(float maxPeak, float floor = 0.5f) {
  const float want = maxPeak * 1.08f;
  return niceCeil(want < floor ? floor : want);
}

// Distance between X axis labels (1, 2, 5, 10, 20, ...) giving at most maxLabels.
inline unsigned labelStep(std::size_t count, unsigned maxLabels) {
  if (maxLabels == 0) maxLabels = 1;
  for (unsigned mag = 1;; mag *= 10)
    for (unsigned m : {1u, 2u, 5u}) {
      const unsigned step = m * mag;
      if ((count + step - 1) / step <= maxLabels) return step;
    }
}

}  // namespace peakchart
