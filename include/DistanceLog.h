// include/DistanceLog.h
#pragma once
#include <cstdio>
#include <cstring>
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

// Formats one log row (see logFileName() for its file) into `buf` (NUL-terminated,
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

// Ride columns appended to a transport row (replacing its '\n'):
// duration_s,ramp_m_s2,rms_x,rms_y,rms_z,peak_x,peak_y,peak_z (m/s2).
inline void appendRideColumns(char *buf, size_t bufSize, float durationS, float ramp, const float rms[3],
                              const float peak[3]) {
  size_t n = std::strlen(buf);
  if (n && buf[n - 1] == '\n') buf[--n] = 0;
  std::snprintf(buf + n, bufSize - n, ",%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", durationS, ramp, rms[0], rms[1],
                rms[2], peak[0], peak[1], peak[2]);
}

// Log file of a row: "B2-B3" for a ride between two stations, "B3_tech" for
// a technological motion at one; `name(s)` gives a station's name
// ("nieznana" for -1). Characters FAT does not allow become '_'.
template <class Name>
inline void logFileName(char *out, size_t outSize, bool technological, int from, int to, Name name) {
  if (technological) std::snprintf(out, outSize, "%s_tech", name(from));
  else std::snprintf(out, outSize, "%s-%s", name(from), name(to));
  for (char *c = out; *c; ++c)
    if (std::strchr("\\/:*?\"<>|", *c) || *c < 32) *c = '_';
}

} // namespace line
