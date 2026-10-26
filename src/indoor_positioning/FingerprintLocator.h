#pragma once
#include <cstddef>
#include <cstdint>
#include "WifiFingerprint.h"

// Position estimate from RSSI fingerprints. Pure computation: no Arduino,
// no heap, testable on a PC.
namespace ips {

enum class PositionStatus : uint8_t { Ok, LowConfidence, NoPosition };
const char *statusName(PositionStatus status);

struct PositionEstimate {
  float xM = 0, yM = 0;         // Interpolated, local frame (metres).
  uint8_t confidence = 0;       // Heuristic match quality 0..100, not an accuracy bound.
  uint8_t apCount = 0;          // Known APs seen in the current scan.
  PositionStatus status = PositionStatus::NoPosition;
  // Per reference point, for diagnostics.
  float distanceDb[kMaxReferencePoints] = {};
  float weight[kMaxReferencePoints] = {};
};

// Weighted RMS RSSI difference (dB) between the current scan and each
// reference, then inverse-distance interpolation of the reference
// coordinates. Works for any refCount >= 2 (up to kMaxReferencePoints).
//
// Per known AP (union of all reference profiles):
//  - weight 1 / (sigma^2 + floor^2) from its calibration spread, scaled by how
//    reliably it was detected, so stable APs dominate;
//  - an AP missing from a profile counts as kMissingRssiDbm there (absence is
//    information), an AP missing from the current scan counts as
//    kMissingRssiDbm with reduced weight (it may be a momentary dropout);
//  - each difference is capped at kDiffCapDb, so one wild AP cannot dominate.
// APs unknown to every profile are ignored. RSSI is compared directly; no
// path-loss model is assumed.
PositionEstimate locate(const Fingerprint *refs, std::size_t refCount, const ScanResult &scan);

} // namespace ips
