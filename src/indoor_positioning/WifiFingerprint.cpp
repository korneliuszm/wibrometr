#include "WifiFingerprint.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ips {

bool Bssid::operator==(const Bssid &o) const { return std::memcmp(b, o.b, sizeof b) == 0; }

const ApReading *ScanResult::find(const Bssid &bssid) const {
  for (uint16_t i = 0; i < count; ++i)
    if (aps[i].bssid == bssid) return &aps[i];
  return nullptr;
}

bool ScanResult::add(const ApReading &reading) {
  if (count >= kMaxScanAps) return false;
  aps[count++] = reading;
  return true;
}

const ApProfile *Fingerprint::find(const Bssid &bssid) const {
  for (uint8_t i = 0; i < count; ++i)
    if (aps[i].bssid == bssid) return &aps[i];
  return nullptr;
}

void FingerprintBuilder::begin(float xM, float yM) {
  xM_ = xM;
  yM_ = yM;
  scans_ = 0;
  count_ = 0;
}

void FingerprintBuilder::addScan(const ScanResult &scan) {
  if (scans_ >= kMaxScansPerPoint) return;
  for (uint16_t i = 0; i < scan.count; ++i) {
    const ApReading &r = scan.aps[i];
    Candidate *c = nullptr;
    for (std::size_t k = 0; k < count_; ++k)
      if (cand_[k].bssid == r.bssid) { c = &cand_[k]; break; }
    if (!c) {
      if (count_ >= kMaxCandidateAps) continue; // Scan lists strongest first.
      c = &cand_[count_++];
      c->bssid = r.bssid;
      std::memcpy(c->ssid, r.ssid, kSsidLen);
      c->ssid[kSsidLen - 1] = '\0';
      c->channel = r.channel;
      c->n = 0;
    }
    if (c->n < kMaxScansPerPoint) c->rssi[c->n++] = r.rssi; // One reading per scan.
  }
  ++scans_;
}

void robustStats(const int8_t *values, int n, float &mean, float &stddev) {
  mean = stddev = 0;
  if (n <= 0) return;
  int8_t sorted[kMaxScansPerPoint];
  n = std::min(n, kMaxScansPerPoint);
  std::copy(values, values + n, sorted);
  std::sort(sorted, sorted + n);
  const float median = n % 2 ? sorted[n / 2] : 0.5f * (sorted[n / 2 - 1] + sorted[n / 2]);
  float dev[kMaxScansPerPoint];
  for (int i = 0; i < n; ++i) dev[i] = std::fabs(values[i] - median);
  std::sort(dev, dev + n);
  const float mad = n % 2 ? dev[n / 2] : 0.5f * (dev[n / 2 - 1] + dev[n / 2]);
  const float band = std::max(kOutlierMinDb, 3.0f * 1.4826f * mad);
  float sum = 0, sumSq = 0;
  int kept = 0;
  for (int i = 0; i < n; ++i) {
    if (std::fabs(values[i] - median) > band) continue;
    sum += values[i];
    sumSq += float(values[i]) * values[i];
    ++kept;
  }
  if (!kept) { mean = median; return; } // Unreachable: the median is always in band.
  mean = sum / kept;
  stddev = std::sqrt(std::max(0.0f, sumSq / kept - mean * mean));
}

std::size_t FingerprintBuilder::build(Fingerprint &out) const {
  out = Fingerprint{};
  out.xM = xM_;
  out.yM = yM_;
  if (scans_ <= 0) return 0;
  // Qualify candidates, then keep the strongest kMaxProfileAps. Only indices
  // and stats are kept here: this runs on the 8 KB loop-task stack.
  uint8_t idx[kMaxCandidateAps];
  float mean[kMaxCandidateAps], stddev[kMaxCandidateAps];
  std::size_t n = 0;
  for (std::size_t k = 0; k < count_; ++k) {
    const Candidate &c = cand_[k];
    if (c.n < kMinDetectionRatio * scans_) continue;
    robustStats(c.rssi, c.n, mean[k], stddev[k]);
    if (mean[k] < kMinMeanRssiDbm || stddev[k] > kMaxRssiStdDb) continue;
    idx[n++] = uint8_t(k);
  }
  std::sort(idx, idx + n, [&](uint8_t a, uint8_t b) { return mean[a] > mean[b]; });
  out.count = uint8_t(std::min(n, kMaxProfileAps));
  for (uint8_t i = 0; i < out.count; ++i) {
    const Candidate &c = cand_[idx[i]];
    ApProfile &p = out.aps[i];
    p.bssid = c.bssid;
    std::memcpy(p.ssid, c.ssid, kSsidLen);
    p.channel = c.channel;
    p.detections = c.n;
    p.scans = uint8_t(scans_);
    p.meanRssi = mean[idx[i]];
    p.stdRssi = stddev[idx[i]];
  }
  return out.count;
}

} // namespace ips
