#pragma once
#include "WifiFingerprint.h"

// Calibration persisted in NVS (namespace "ips"), so it survives restarts.
// Only written when a calibration completes or is deleted, never per fix.
namespace ips {

struct Calibration {
  float sideM = 0;
  uint8_t pointCount = 0;
  Fingerprint points[kMaxReferencePoints];
};

bool loadCalibration(Calibration &out);  // false: none stored or unreadable.
bool saveCalibration(const Calibration &cal);
void eraseCalibration();

} // namespace ips
