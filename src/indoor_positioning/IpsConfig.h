#pragma once
#include <cstddef>
#include <cstdint>

// Indoor positioning settings. Values marked "-D" can be overridden from
// platformio.ini build_flags, e.g. -D CALIBRATION_SIDE_M=4.0f.

#ifndef CALIBRATION_SIDE_M
#define CALIBRATION_SIDE_M 5.0f // Side of the equilateral calibration triangle.
#endif
#ifndef IPS_SCANS_PER_POINT
#define IPS_SCANS_PER_POINT 15
#endif
#ifndef IPS_DEBUG
#define IPS_DEBUG 0 // 1: verbose diagnostics on Serial.
#endif

namespace ips {

constexpr float kCalibrationSideM = CALIBRATION_SIDE_M;
constexpr int kScansPerPoint = IPS_SCANS_PER_POINT;
constexpr int kMaxScansPerPoint = 30;
static_assert(kScansPerPoint >= 3 && kScansPerPoint <= kMaxScansPerPoint, "3..30 scans per point");

// Capacity limits: everything is statically sized, no heap per scan.
constexpr std::size_t kMaxReferencePoints = 3; // A, B, C; the algorithm takes any count >= 2.
constexpr std::size_t kMaxScanAps = 48;        // APs taken from one scan (strongest first).
constexpr std::size_t kMaxCandidateAps = 64;   // Distinct BSSIDs tracked while calibrating a point.
constexpr std::size_t kMaxProfileAps = 24;     // APs kept per reference point.
constexpr std::size_t kSsidLen = 33;           // 32 chars + NUL.

// Calibration filters for one AP at one point.
constexpr float kMinDetectionRatio = 0.5f; // Seen in at least half of the scans.
constexpr float kMinMeanRssiDbm = -90.0f;  // Weaker APs are too unreliable.
constexpr float kMaxRssiStdDb = 8.0f;      // More variable APs are dropped.
constexpr float kOutlierMinDb = 6.0f;      // Outlier band: max(this, 3 * robust sigma).

// Matching.
constexpr float kMissingRssiDbm = -100.0f; // Stand-in for "not visible".
constexpr float kMissingScanWeight = 0.5f; // Not seen now may just be a dropout.
#ifndef IPS_DIFF_CAP_DB
#define IPS_DIFF_CAP_DB 12.0f
#endif
#ifndef IPS_IDW_POWER
#define IPS_IDW_POWER 3.0f
#endif
#ifndef IPS_TRIM_APS
#define IPS_TRIM_APS 1
#endif
constexpr float kDiffCapDb = IPS_DIFF_CAP_DB; // Limits the pull of any single AP.
constexpr int kTrimAps = IPS_TRIM_APS;        // Worst-matching APs ignored per reference.
constexpr std::size_t kMinApsForTrim = 5;     // Trim only when enough APs remain.
constexpr float kSigmaFloorDb = 2.0f;      // Keeps weights finite for very stable APs.
constexpr float kIdwPower = IPS_IDW_POWER;  // Interpolation weight = 1 / (d + 1 dB)^p.
constexpr std::size_t kMinApCount = 3;     // Fewer matched APs: no position.
constexpr uint8_t kOkConfidence = 40;      // Below: LOW_CONFIDENCE.

// Scanning cadence.
constexpr uint32_t kScanIntervalMs = 2500;  // Positioning update period.
constexpr uint32_t kScanMsPerChannel = 120; // Active scan dwell; ~1.6 s for 13 channels.

// Position filter.
constexpr float kEmaAlpha = 0.35f;  // Share of a new fix taken at full confidence.
constexpr float kMaxJumpM = 2.0f;   // Bigger jumps need a second confirming fix.
constexpr int kJumpConfirmFixes = 2;
constexpr float kDeadzoneM = 0.15f; // Displayed point ignores smaller changes.

} // namespace ips
