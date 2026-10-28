// include/DistanceLog.h
#pragma once
#include <cstdio>
#include "MotionClassifier.h"

namespace line {

inline const char *motionName(Motion m) {
  switch (m) {
    case Motion::None: return "";
    case Motion::Lift: return "lift";
    case Motion::RotationZ: return "rotation_z";
    case Motion::ZTransfer: return "z_transfer";
    case Motion::Compound: return "compound";
  }
  return "";
}

// Formats one row of distances_last_run.csv into `buf` (NUL-terminated,
// truncated if `bufSize` is too small). Columns: timestamp_ms,segment_type,
// from_station,to_station,station_id,motion_type,axis,measured_distance_m,
// measured_angle_deg,confidence,quality_flags
inline void formatDistanceRow(char *buf, size_t bufSize, uint32_t timestampMs,
                               const char *segmentType, int from, int to,
                               const char *stationId, Motion motion, const char *axis,
                               float measuredDistanceM, float measuredAngleDeg,
                               float confidence, const char *qualityFlags) {
  char fromStr[4] = "", toStr[4] = "";
  if (from >= 0) std::snprintf(fromStr, sizeof(fromStr), "%d", from);
  if (to >= 0) std::snprintf(toStr, sizeof(toStr), "%d", to);
  char distStr[16] = "", angleStr[16] = "";
  if (measuredDistanceM != 0 || motion == Motion::None)
    std::snprintf(distStr, sizeof(distStr), "%.3f", measuredDistanceM);
  if (measuredAngleDeg != 0) std::snprintf(angleStr, sizeof(angleStr), "%.3f", measuredAngleDeg);
  std::snprintf(buf, bufSize, "%u,%s,%s,%s,%s,%s,%s,%s,%s,%.2f,%s\n",
                (unsigned)timestampMs, segmentType, fromStr, toStr, stationId,
                motionName(motion), axis, distStr, angleStr, confidence, qualityFlags);
}

} // namespace line
