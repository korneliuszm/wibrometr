// include/StationConfig.h
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "LineTracker.h" // line::maxStations

namespace line {

enum class Direction : uint8_t { XPlus, XMinus, YPlus, YMinus };
enum class Motion : uint8_t { None, Lift, RotationZ, ZTransfer, Compound };

struct StationRow {
  char id[12] = {};
  Direction direction = Direction::XPlus;
  float distanceM = 0;
  bool passthrough = false;
  bool wifiScan = true;
  Motion motion = Motion::None;
  float motionRange = 0;  // metres (lift/z_transfer) or degrees (rotation_z); 0 = unset.
  float motionSpeed = 0;  // m/s or deg/s; 0 = unset (optional column).
};

struct StationConfig {
  static constexpr int maxRows = maxStations;
  int count = 0;
  StationRow row[maxRows];
};

namespace detail {
// Copies the next ','-separated field from `*p` into `out` (NUL-terminated,
// truncated to outSize - 1) and advances `*p` past the separator. Treats
// '\n' and end-of-string as the end of the row (does not advance past them).
inline void nextField(const char *&p, char *out, int outSize) {
  int n = 0;
  while (*p && *p != ',' && *p != '\n' && *p != '\r') {
    if (n < outSize - 1) out[n++] = *p;
    ++p;
  }
  out[n] = 0;
  if (*p == ',') ++p;
}
inline bool parseBool(const char *s, bool &out) {
  if (std::strcmp(s, "true") == 0) { out = true; return true; }
  if (std::strcmp(s, "false") == 0) { out = false; return true; }
  return false;
}
inline bool parseDirection(const char *s, Direction &out) {
  if (std::strcmp(s, "X+") == 0) { out = Direction::XPlus; return true; }
  if (std::strcmp(s, "X-") == 0) { out = Direction::XMinus; return true; }
  if (std::strcmp(s, "Y+") == 0) { out = Direction::YPlus; return true; }
  if (std::strcmp(s, "Y-") == 0) { out = Direction::YMinus; return true; }
  return false;
}
inline bool parseMotion(const char *s, Motion &out) {
  if (s[0] == 0 || std::strcmp(s, "none") == 0) { out = Motion::None; return true; }
  if (std::strcmp(s, "lift") == 0) { out = Motion::Lift; return true; }
  if (std::strcmp(s, "rotation_z") == 0) { out = Motion::RotationZ; return true; }
  if (std::strcmp(s, "z_transfer") == 0) { out = Motion::ZTransfer; return true; }
  if (std::strcmp(s, "compound") == 0) { out = Motion::Compound; return true; }
  return false;
}
inline bool isHeaderRow(const char *line) { return std::strncmp(line, "station_id,", 11) == 0; }

inline bool parseRow(const char *line, StationRow &out) {
  const char *p = line;
  char field[16];
  StationRow r;

  detail::nextField(p, field, sizeof(field));
  std::strncpy(r.id, field, sizeof(r.id) - 1);

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseDirection(field, r.direction)) return false;

  detail::nextField(p, field, sizeof(field));
  if (field[0] == 0) return false;
  r.distanceM = std::strtof(field, nullptr);

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseBool(field, r.passthrough)) return false;

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseBool(field, r.wifiScan)) return false;

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseMotion(field, r.motion)) return false;

  detail::nextField(p, field, sizeof(field)); // motion_axis: accepted, not used (rotation_z is always Z).
  (void)field;

  detail::nextField(p, field, sizeof(field)); // motion_range
  r.motionRange = field[0] ? std::strtof(field, nullptr) : 0;

  detail::nextField(p, field, sizeof(field)); // motion_speed
  r.motionSpeed = field[0] ? std::strtof(field, nullptr) : 0;

  if (*p != 0 && *p != '\n' && *p != '\r') return false;

  out = r;
  return true;
}
} // namespace detail

// Parses the whole file text (rows separated by '\n', CRLF tolerated, an
// optional header row is skipped). All-or-nothing: a single malformed or
// unrecognised row rejects the whole file (caller keeps the previous
// configuration, i.e. none, and falls back to auto-learning).
inline bool parseStationConfig(const char *text, StationConfig &out) {
  StationConfig result;
  const char *p = text;
  while (*p) {
    const char *lineStart = p;
    while (*p && *p != '\n') ++p;
    const char *lineEnd = p;
    if (*p == '\n') ++p;
    if (lineEnd == lineStart) continue; // blank line
    char buf[160];
    int n = int(lineEnd - lineStart);
    if (n >= (int)sizeof(buf)) return false; // row too long to be well-formed
    std::memcpy(buf, lineStart, n);
    buf[n] = 0;
    if (detail::isHeaderRow(buf)) continue;
    if (result.count >= StationConfig::maxRows) return false; // more rows than maxStations
    if (!detail::parseRow(buf, result.row[result.count])) return false;
    ++result.count;
  }
  if (result.count == 0) return false;
  out = result;
  return true;
}

} // namespace line
