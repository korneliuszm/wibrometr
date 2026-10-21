#pragma once
#include "BleRanging.h"
#include "BleScanner.h"

#ifndef BLE_TARGET_ADDRESS
#define BLE_TARGET_ADDRESS "5A:23:BE:FC:D0:B1" // Default target; BOOT can adopt another.
#endif

// Distance to one BLE device: scanner + tracker + settings kept in NVS
// (target address and the RSSI measured at 1 m). Driven from loop(now).
namespace ble {

struct DistanceView {
  bool scanning = false;
  Address target;
  bool seen = false, fresh = false, apple = false;
  uint32_t ageMs = 0;
  float distanceM = 0;     // NaN when no recent RSSI.
  float rssiMedian = -128; // -128 when no recent RSSI.
  int8_t rssiLast = 0;
  uint32_t intervalMs = 0;
  float rssiAt1m = kDefaultRssiAt1mDbm;
  bool haveCandidate = false;
  Candidate candidate;     // Strongest other device.
  std::size_t nearbyCount = 0;
};

class BleDistance {
public:
  void begin();
  void loop(uint32_t now);
  // Makes the strongest other device the target; false if none heard.
  bool adoptStrongest(uint32_t now, Address &adopted);
  // Uses the current median RSSI as the 1 m reference; false without signal.
  bool calibrate1m(uint32_t now, float &rssiAt1m);
  DistanceView view(uint32_t now) const;

private:
  void save() const;
  BleScanner scanner_;
  Tracker tracker_;
  bool running_ = false;
  uint32_t nextLogMs_ = 0;
};

} // namespace ble
