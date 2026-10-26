#include "FingerprintLocator.h"
#include <algorithm>
#include <cmath>

namespace ips {

const char *statusName(PositionStatus status) {
  switch (status) {
  case PositionStatus::Ok: return "OK";
  case PositionStatus::LowConfidence: return "LOW_CONFIDENCE";
  default: return "NO_POSITION";
  }
}

namespace {

constexpr std::size_t kMaxKnown = kMaxReferencePoints * kMaxProfileAps;

// Union of all reference APs with their per-reference RSSI and weight.
struct KnownAps {
  std::size_t count = 0;
  Bssid bssid[kMaxKnown];
  float rssi[kMaxKnown][kMaxReferencePoints]; // kMissingRssiDbm when absent.
  float weight[kMaxKnown];
};

void collect(const Fingerprint *refs, std::size_t refCount, KnownAps &k) {
  k.count = 0;
  for (std::size_t r = 0; r < refCount; ++r)
    for (uint8_t i = 0; i < refs[r].count; ++i) {
      const Bssid &id = refs[r].aps[i].bssid;
      bool seen = false;
      for (std::size_t j = 0; j < k.count && !seen; ++j) seen = k.bssid[j] == id;
      if (!seen && k.count < kMaxKnown) k.bssid[k.count++] = id;
    }
  for (std::size_t j = 0; j < k.count; ++j) {
    float sigma = 0, reliability = 0;
    int present = 0;
    for (std::size_t r = 0; r < refCount; ++r) {
      const ApProfile *p = refs[r].find(k.bssid[j]);
      k.rssi[j][r] = p ? p->meanRssi : kMissingRssiDbm;
      if (!p) continue;
      sigma += p->stdRssi;
      reliability += p->scans ? float(p->detections) / p->scans : 0;
      ++present;
    }
    sigma /= present; // present >= 1: every known AP comes from some profile.
    reliability /= present;
    k.weight[j] = reliability / (sigma * sigma + kSigmaFloorDb * kSigmaFloorDb);
  }
}

// Weighted RMS of capped differences; obs[j] / obsWeight[j] describe the
// measurement compared against reference column r; baseline[j] (optional) is
// subtracted from AP j's squared difference. The kTrimAps largest
// contributions are dropped (when enough APs are seen), so one AP that is
// blocked, moved or reflecting cannot drag the match on its own.
float cappedSq(float d) {
  d = std::max(-kDiffCapDb, std::min(kDiffCapDb, d));
  return d * d;
}

float distanceTo(const KnownAps &k, std::size_t r, const float *obs, const float *obsWeight,
                 std::size_t seenAps, const float *baseline = nullptr) {
  float num = 0, den = 0;
  float worstTerm[kTrimAps > 0 ? kTrimAps : 1] = {}, worstWeight[kTrimAps > 0 ? kTrimAps : 1] = {};
  const int trim = seenAps >= kMinApsForTrim ? kTrimAps : 0;
  for (std::size_t j = 0; j < k.count; ++j) {
    const float w = k.weight[j] * obsWeight[j];
    const float term = w * (cappedSq(obs[j] - k.rssi[j][r]) - (baseline ? baseline[j] : 0));
    num += term;
    den += w;
    // Keep the `trim` largest terms (insertion into a tiny sorted list).
    for (int t = 0; t < trim; ++t)
      if (term > worstTerm[t]) {
        for (int m = trim - 1; m > t; --m) {
          worstTerm[m] = worstTerm[m - 1];
          worstWeight[m] = worstWeight[m - 1];
        }
        worstTerm[t] = term;
        worstWeight[t] = w;
        break;
      }
  }
  for (int t = 0; t < trim; ++t) {
    num -= worstTerm[t];
    den -= worstWeight[t];
  }
  return den > 1e-12f ? std::sqrt(std::max(0.0f, num) / den) : kDiffCapDb;
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

} // namespace

PositionEstimate locate(const Fingerprint *refs, std::size_t refCount, const ScanResult &scan) {
  PositionEstimate out;
  refCount = std::min(refCount, kMaxReferencePoints);
  if (!refs || refCount < 2) return out;

  static KnownAps k; // ~1 KB: kept off the loop-task stack; locate is not reentrant.
  collect(refs, refCount, k);
  if (!k.count) return out;

  float obs[kMaxKnown], obsWeight[kMaxKnown];
  float seenWeight = 0, totalWeight = 0;
  for (std::size_t j = 0; j < k.count; ++j) {
    const ApReading *now = scan.find(k.bssid[j]);
    obs[j] = now ? float(now->rssi) : kMissingRssiDbm;
    obsWeight[j] = now ? 1.0f : kMissingScanWeight;
    if (now) {
      ++out.apCount;
      seenWeight += k.weight[j];
    }
    totalWeight += k.weight[j];
  }
  // An AP missing from this scan says "it is weak here", not how weak: only
  // its difference between references is used. Its smallest squared
  // difference is common to all references and is removed; otherwise an AP
  // that is simply off would add the same penalty everywhere and pull every
  // fix towards the centre.
  float baseline[kMaxKnown];
  for (std::size_t j = 0; j < k.count; ++j) {
    baseline[j] = 0;
    if (obsWeight[j] == 1.0f) continue;
    baseline[j] = cappedSq(obs[j] - k.rssi[j][0]);
    for (std::size_t r = 1; r < refCount; ++r)
      baseline[j] = std::min(baseline[j], cappedSq(obs[j] - k.rssi[j][r]));
  }
  for (std::size_t r = 0; r < refCount; ++r)
    out.distanceDb[r] = distanceTo(k, r, obs, obsWeight, out.apCount, baseline);
  if (out.apCount < kMinApCount) return out; // NoPosition, distances kept for diagnostics.

  // Inverse-distance weights; +1 dB keeps a perfect match finite.
  float sum = 0, dMin = out.distanceDb[0];
  for (std::size_t r = 0; r < refCount; ++r) {
    out.weight[r] = 1.0f / std::pow(out.distanceDb[r] + 1.0f, kIdwPower);
    sum += out.weight[r];
    dMin = std::min(dMin, out.distanceDb[r]);
  }
  for (std::size_t r = 0; r < refCount; ++r) {
    out.weight[r] /= sum;
    out.xM += out.weight[r] * refs[r].xM;
    out.yM += out.weight[r] * refs[r].yM;
  }

  // Separation: how distinguishable the references are from each other. If
  // they look alike, any interpolated position is meaningless.
  float separation = kDiffCapDb;
  float ones[kMaxKnown];
  std::fill(ones, ones + k.count, 1.0f);
  for (std::size_t a = 0; a < refCount; ++a)
    for (std::size_t b = a + 1; b < refCount; ++b) {
      float col[kMaxKnown];
      for (std::size_t j = 0; j < k.count; ++j) col[j] = k.rssi[j][a];
      separation = std::min(separation, distanceTo(k, b, col, ones, k.count));
    }

  const float match = clamp01(1.0f - (dMin - 3.0f) / 12.0f);  // <=3 dB: 1, >=15 dB: 0.
  const float coverage = totalWeight > 0 ? seenWeight / totalWeight : 0;
  const float distinct = clamp01(separation / 8.0f);          // >=8 dB apart: 1.
  const float apFactor = clamp01(out.apCount / 6.0f);
  const float conf = 100.0f * match * std::sqrt(coverage) * distinct * (0.5f + 0.5f * apFactor);
  out.confidence = uint8_t(std::lround(std::max(0.0f, std::min(100.0f, conf))));
  out.status = out.confidence >= kOkConfidence ? PositionStatus::Ok : PositionStatus::LowConfidence;
  return out;
}

} // namespace ips
