#pragma once
#include <cstdint>

// Start trigger from the VL53L0X range readings. Single readings are not trusted: the sensor
// throws wrong values now and then (it did while it was in a bad state), and a wrong value
// is as likely to be near as far. A measurement starts when, among the last kWindow readings,
// at least kNeeded are closer than the trigger distance AND agree with each other: the
// largest and the smallest of them differ by at most max(kClusterMm, 20 % of the largest).
// A real object in front of the sensor gives such a cluster; scattered wrong values do not.
// The object has to stay in view for kNeeded readings (about 100 ms at the 33 ms setting,
// 60 ms at the 20 ms one).
//
// It fires once; the next one needs the object to go away first (kNeeded of the last
// kWindow readings beyond the trigger distance plus kHysteresisMm, or without echo), so
// something standing in front of the sensor does not chain files. No hardware; tested on a PC.
class RangeTrigger {
public:
  static constexpr uint16_t kNoReading = 0xFFFF;  // Out of range / invalid reading: counts as "far".
  static constexpr int kWindow = 5;
  static constexpr int kNeeded = 3;
  static constexpr uint16_t kClusterMm = 40;
  static constexpr uint16_t kHysteresisMm = 20;
  enum class Event : uint8_t { None, Start };

  // Call once per new reading.
  Event update(uint16_t mm, uint16_t triggerMm) {
    push(mm);
    if (fired_) {
      int far = 0;
      for (int i = 0; i < kWindow; ++i) {
        if (!seen(i)) continue;
        far += last_[i] == kNoReading || last_[i] >= uint32_t(triggerMm) + kHysteresisMm;
      }
      if (far >= kNeeded) {
        fired_ = false;
        clear();
      }
      return Event::None;
    }
    int near = 0;
    uint16_t lo = kNoReading, hi = 0;
    for (int i = 0; i < kWindow; ++i) {
      if (!seen(i) || last_[i] == kNoReading || last_[i] >= triggerMm) continue;
      ++near;
      if (last_[i] < lo) lo = last_[i];
      if (last_[i] > hi) hi = last_[i];
    }
    if (near < kNeeded) return Event::None;
    const uint16_t tolerance = hi / 5 > kClusterMm ? uint16_t(hi / 5) : kClusterMm;
    if (hi - lo > tolerance) return Event::None;
    fired_ = true;
    clear();
    return Event::Start;
  }

  bool fired() const { return fired_; }
  // Forgets the readings in the window (not the fired state): after a pause in ranging, old
  // readings must not count together with new ones.
  void clearWindow() { clear(); }

private:
  void push(uint16_t mm) {
    last_[head_] = mm;
    head_ = (head_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;
  }
  void clear() { count_ = head_ = 0; }
  bool seen(int i) const { return i < count_; }  // Slots are filled from 0 until the window is full.

  uint16_t last_[kWindow] = {};
  int head_ = 0, count_ = 0;
  bool fired_ = false;
};
