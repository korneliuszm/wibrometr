#pragma once
#include <cmath>
#include "StationConfig.h"

namespace line {

// One technological motion observed at a station, classified from
// measurements the Tracker already produces for a Parked -> Moving ->
// Parked cycle that did NOT change the counted station (see
// Tracker::finish()). Never influences which station is current — that is
// a hard requirement from the spec, enforced by the caller never writing
// `station_` from this result, not by anything in here.
struct MotionResult {
  bool valid = false;
  Motion motion = Motion::None;
  float measuredDistanceM = 0; // Lift / z_transfer / compound.
  float measuredAngleDeg = 0;  // RotationZ.
  bool overRange = false;      // Measured value exceeds 1.5x the configured range.
};

// `distanceM`/`angleDeg` are whichever of the two the caller measured for
// this cycle (the other is 0) — the Tracker knows which one applies from
// `row.motion` before calling this, same as this function does.
inline MotionResult classifyMotion(const StationRow &row, float distanceM, float angleDeg) {
  MotionResult r;
  if (row.motion == Motion::None) return r;
  r.valid = true;
  r.motion = row.motion;
  const bool isRotation = row.motion == Motion::RotationZ;
  r.measuredDistanceM = isRotation ? 0 : distanceM;
  r.measuredAngleDeg = isRotation ? angleDeg : 0;
  const float measured = isRotation ? angleDeg : distanceM;
  // The range's sign is a direction (rotation_z); its size is the range.
  r.overRange = row.motionRange != 0 && std::fabs(measured) > 1.5f * std::fabs(row.motionRange);
  return r;
}

} // namespace line
