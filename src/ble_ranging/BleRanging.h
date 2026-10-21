#pragma once
#include <cstddef>
#include <cstdint>

// Distance to one BLE advertiser from RSSI. Pure computation (no Arduino,
// no heap), tested on a PC.
//
// Distance uses the log-distance model d = 10^((P1m - RSSI) / (10 n)), with
// P1m the RSSI at 1 m and n the path-loss exponent. It is a rough estimate:
// bodies, walls, antenna orientation and reflections easily change RSSI by
// 5-10 dB, i.e. a factor of 2 or more in distance at n = 2.
namespace ble {

constexpr float kDefaultRssiAt1mDbm = -59.0f; // Typical phone; same as common scanner apps.
constexpr float kPathLossExponent = 2.0f;     // Free space 2, indoors often 2-3.
constexpr uint32_t kRssiWindowMs = 3000;      // Median over the last 3 s of adverts.
constexpr std::size_t kRssiSamples = 16;
constexpr uint32_t kFreshMs = 3000, kLostMs = 10000;
constexpr std::size_t kMaxCandidates = 24;    // Nearby devices tracked for "adopt".

// Address in display order (most significant byte first, as phones show it).
struct Address {
  uint8_t b[6] = {};
  bool operator==(const Address &o) const;
  bool empty() const;
  // Random private resolvable address: top bits 01. Apple and Android rotate
  // these about every 15 minutes, so a stored target eventually disappears.
  bool isResolvablePrivate() const { return (b[0] & 0xC0) == 0x40; }
  void format(char *out, std::size_t len) const; // "5A:23:BE:FC:D0:B1"
  static bool parse(const char *text, Address &out);
};

float distanceM(float rssiDbm, float rssiAt1mDbm, float exponent = kPathLossExponent);

// Robust RSSI level: median of the samples in the last kRssiWindowMs.
class RssiFilter {
public:
  void add(int8_t rssi, uint32_t nowMs);
  void clear() { head_ = count_ = 0; }
  bool valid(uint32_t nowMs) const { return median(nowMs) > -128; }
  // -128 when no recent sample.
  float median(uint32_t nowMs) const;
  std::size_t recent(uint32_t nowMs) const;

private:
  int8_t rssi_[kRssiSamples] = {};
  uint32_t ms_[kRssiSamples] = {};
  std::size_t head_ = 0, count_ = 0;
};

struct Advert {
  Address addr;
  int8_t rssi = 0;
  bool apple = false; // Apple manufacturer data (company 0x004C).
  uint32_t ms = 0;
};

struct Candidate {
  Address addr;
  float rssi = -128; // Smoothed (EMA).
  bool apple = false;
  uint32_t lastMs = 0;
};

// Follows one target address and remembers the nearby devices.
class Tracker {
public:
  void setTarget(const Address &addr);
  const Address &target() const { return target_; }
  void setRssiAt1m(float dbm) { rssiAt1m_ = dbm; }
  float rssiAt1m() const { return rssiAt1m_; }

  void add(const Advert &a);

  bool seen() const { return seenMs_ != 0; }
  uint32_t lastSeenMs() const { return seenMs_; }
  int8_t lastRssi() const { return lastRssi_; }
  bool targetApple() const { return targetApple_; }
  float smoothedRssi(uint32_t nowMs) const { return filter_.median(nowMs); }
  bool fresh(uint32_t nowMs) const;
  uint32_t ageMs(uint32_t nowMs) const; // Since the target was last heard.
  // NaN when there is no recent RSSI.
  float distance(uint32_t nowMs) const;
  // Typical spacing of received adverts (EMA of gaps), 0 if unknown.
  uint32_t advertIntervalMs() const { return uint32_t(intervalMs_ + 0.5f); }
  // Strongest device heard in the last kFreshMs, excluding the target.
  bool strongest(uint32_t nowMs, Candidate &out) const;
  std::size_t candidateCount(uint32_t nowMs) const;

private:
  Address target_;
  RssiFilter filter_;
  float rssiAt1m_ = kDefaultRssiAt1mDbm;
  uint32_t seenMs_ = 0;
  int8_t lastRssi_ = 0;
  bool targetApple_ = false;
  float intervalMs_ = 0;
  Candidate cand_[kMaxCandidates];
  std::size_t candCount_ = 0;
};

} // namespace ble
