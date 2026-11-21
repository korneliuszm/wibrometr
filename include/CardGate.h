#pragma once
#include <atomic>

// Keeps the main loop from taking the card back while a USB transfer is running.
// USB task: enter() before touching the card and leave() after it; enter() false
// means "revoked, report busy". Main loop: release() until it returns true (no
// transfer in flight, and every later enter() fails), then the card may be
// remounted; grant() hands it to USB again. Each side stores its own flag and
// then loads the other's (seq_cst), so they cannot both go ahead.
class CardGate {
public:
  bool enter() {
    inflight_.store(true);
    if (!revoked_.load()) return true;
    inflight_.store(false);
    return false;
  }
  void leave() { inflight_.store(false); }

  bool release() {
    revoked_.store(true);
    return !inflight_.load();
  }
  void grant() { revoked_.store(false); }

private:
  std::atomic<bool> inflight_{false}, revoked_{false};
};
