#pragma once
#include <cstddef>
#include <cstdint>
#include "IpsConfig.h"

// Plain data types and calibration aggregation. No Arduino dependencies, so
// this compiles and is tested on a PC.
namespace ips {

struct Bssid {
  uint8_t b[6] = {};
  bool operator==(const Bssid &o) const;
};

struct ApReading {
  Bssid bssid;
  int8_t rssi = 0;
  uint8_t channel = 0;
  char ssid[kSsidLen] = {}; // Empty for hidden networks.
};

struct ScanResult {
  uint16_t count = 0;
  uint32_t startMs = 0;    // Device clock when the scan began.
  uint32_t durationMs = 0;
  ApReading aps[kMaxScanAps];
  // Returns the reading for a BSSID, or nullptr when it was not seen.
  const ApReading *find(const Bssid &bssid) const;
  // Appends while there is room; returns false when full.
  bool add(const ApReading &reading);
};

// One AP as seen at one reference point.
struct ApProfile {
  Bssid bssid;
  char ssid[kSsidLen] = {};
  uint8_t channel = 0;
  uint8_t detections = 0; // Scans that saw it.
  uint8_t scans = 0;      // Scans taken at the point.
  float meanRssi = 0;     // dBm, outliers removed.
  float stdRssi = 0;      // dB, outliers removed.
};

// Signal profile of one reference point at local coordinates (metres).
struct Fingerprint {
  float xM = 0, yM = 0;
  uint8_t count = 0;
  ApProfile aps[kMaxProfileAps];
  const ApProfile *find(const Bssid &bssid) const;
};

// Collects the scans taken at one point and turns them into a Fingerprint.
class FingerprintBuilder {
public:
  void begin(float xM, float yM);
  void addScan(const ScanResult &scan);
  int scans() const { return scans_; }
  // Aggregates; returns the number of APs kept (0 = unusable point).
  std::size_t build(Fingerprint &out) const;

private:
  struct Candidate {
    Bssid bssid;
    char ssid[kSsidLen];
    uint8_t channel;
    uint8_t n;
    int8_t rssi[kMaxScansPerPoint];
  };
  float xM_ = 0, yM_ = 0;
  int scans_ = 0;
  std::size_t count_ = 0;
  Candidate cand_[kMaxCandidateAps];
};

// Robust mean/std of RSSI samples: values further than max(kOutlierMinDb,
// 3 * 1.4826 * MAD) from the median are discarded. Exposed for tests.
void robustStats(const int8_t *values, int n, float &mean, float &stddev);

} // namespace ips
