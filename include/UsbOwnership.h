#pragma once
#include <atomic>
#include <cstdint>

// Who owns the SD card: the USB host (MSC) or the recorder. Written from the
// USB task callbacks, read from the main loop. All state lives in one atomic
// word so transitions stay coherent.
class UsbOwnership {
public:
  void setEnumerated(bool on) {
    if (on) {
      uint8_t cur = state_.load();
      while (!(cur & kEnumerated) && !state_.compare_exchange_weak(cur, kEnumerated)) {}
    } else {
      state_.store(0);  // disconnect also clears a pending eject
    }
  }
  void onEject() { update([](uint8_t s) { return (s & kEnumerated) ? uint8_t(s | kEjected) : s; }); }
  // Sector access after an eject means the host took the card back. Without an
  // enumerated host it is ignored.
  void onAccess() { update([](uint8_t s) { return (s & kEnumerated) ? uint8_t(s & ~kEjected) : s; }); }
  bool hostOwns() const {
    const uint8_t s = state_.load();
    return (s & kEnumerated) && !(s & kEjected);
  }

private:
  static constexpr uint8_t kEnumerated = 1, kEjected = 2;
  template <class F> void update(F f) {
    uint8_t cur = state_.load();
    while (!state_.compare_exchange_weak(cur, f(cur))) {}
  }
  std::atomic<uint8_t> state_{0};
};
