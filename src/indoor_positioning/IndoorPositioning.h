#pragma once
#include <cstdint>
#include "FingerprintLocator.h"
#include "FingerprintStore.h"
#include "PositionFilter.h"
#include "WifiFingerprint.h"
#include "WifiScanner.h"

// Indoor positioning by Wi-Fi RSSI fingerprinting: calibration at the corners
// of an equilateral triangle, then periodic scans matched against them.
// Everything is driven from loop(now) without blocking; the UI only calls the
// API below and reads the results.
namespace ips {

struct CalibrationProgress {
  bool active = false;
  uint8_t point = 0;       // 0 = A, 1 = B, 2 = C.
  uint8_t pointCount = kMaxReferencePoints;
  bool measuring = false;  // false: waiting for the user to stand at the point.
  uint8_t scansDone = 0;
  uint8_t scansTotal = kScansPerPoint;
  bool lastPointFailed = false; // Too few usable APs: the point must be redone.
};

struct Position {
  bool valid = false;            // A fix has been shown since positioning started.
  float xM = 0, yM = 0;          // Filtered, displayed position.
  float rawXM = 0, rawYM = 0;    // Last unfiltered fix.
  uint8_t confidence = 0;
  uint8_t apCount = 0;
  PositionStatus status = PositionStatus::NoPosition;
  uint32_t updatedMs = 0;        // millis() of the last scan evaluated.
  uint32_t scanMs = 0;           // Duration of that scan.
};

class IndoorPositioning {
public:
  void begin();                  // Loads a stored calibration; call once.
  void loop(uint32_t now);       // Call often; never blocks.

  void startCalibration();       // Keeps the old calibration until the new one completes.
  void confirmCalibrationPoint();// User stands at the current point: start measuring.
  void cancelCalibration();
  void resetCalibration();       // Deletes the stored calibration.
  bool isCalibrated() const { return calibrated_; }
  CalibrationProgress getCalibrationProgress() const { return progress_; }

  void startPositioning();       // Periodic scans while active (and calibrated).
  void stopPositioning();
  bool isPositioning() const { return positioning_; }
  Position getPosition() const { return position_; }

  // Reference point coordinates for the current side length (or the stored
  // calibration, once there is one).
  float sideM() const { return calibrated_ ? cal_.sideM : kCalibrationSideM; }
  void referencePoint(uint8_t index, float &xM, float &yM) const;

private:
  void onScan(const ScanResult &scan);
  void finishPoint();
  void evaluate(const ScanResult &scan);
  void dumpCalibration() const;

  WifiScanner scanner_;
  FingerprintBuilder builder_;
  PositionFilter filter_;
  Calibration cal_, pending_;
  CalibrationProgress progress_;
  Position position_;
  ScanResult scan_;
  bool calibrated_ = false, positioning_ = false;
  uint32_t nextScanMs_ = 0;
  uint32_t measureFromMs_ = 0; // Calibration scans must start after the user confirmed.
};

} // namespace ips
