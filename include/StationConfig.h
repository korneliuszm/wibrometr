// include/StationConfig.h
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace line {

// X/Y: a stretch of rollers on the floor; Z: a lift shaft between two stops
// (elevator), up or down; RZ: a turntable between two positions, turning the
// body about the shown Z axis (pointing down): RZ+ right (CW from above),
// RZ- left, by `distanceM` degrees (a multiple of 90).
enum class Direction : uint8_t { XPlus, XMinus, YPlus, YMinus, ZPlus, ZMinus, RZPlus, RZMinus };
// Technological motion at a station (the station does not change):
// Lift: one move in Z, either way. RpsSeat: the body set down on the
// reference (RPS) pins and lifted back, two moves in Z, either way first.
// RotationZ: a turn about Z by the signed range (a multiple of 90 degrees).
enum class Motion : uint8_t { None, Lift, RpsSeat, RotationZ, ZTransfer, Compound };

struct StationRow {
  char id[12] = {};
  Direction direction = Direction::XPlus;
  float distanceM = 0;
  bool wifiScan = true;
  Motion motion = Motion::None;
  float motionRange = 0;  // metres (lift / RPS_seat / z_transfer) or degrees (rotation_z); 0 = unset.
};

struct StationConfig {
  // Mirrors line::maxStations (LineTracker.h); duplicated so this header
  // does not need to include LineTracker.h (which would otherwise create a
  // circular include between StationConfig.h and LineTracker.h). Checked
  // against the real line::maxStations by a static_assert in LineTracker.h,
  // once both are visible there.
  static constexpr int maxRows = 64;
  int count = 0;
  StationRow row[maxRows];
};

// Why a file was rejected: the line (from 1, counting every line of the
// file) and a short reason; line 0 when it is about the file as a whole.
struct ConfigError {
  int line = 0;
  const char *what = "";
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
  if (std::strcmp(s, "Z+") == 0) { out = Direction::ZPlus; return true; }
  if (std::strcmp(s, "Z-") == 0) { out = Direction::ZMinus; return true; }
  if (std::strcmp(s, "RZ+") == 0) { out = Direction::RZPlus; return true; }
  if (std::strcmp(s, "RZ-") == 0) { out = Direction::RZMinus; return true; }
  return false;
}
inline bool parseMotion(const char *s, Motion &out) {
  if (s[0] == 0 || std::strcmp(s, "none") == 0) { out = Motion::None; return true; }
  if (std::strcmp(s, "lift") == 0) { out = Motion::Lift; return true; }
  if (std::strcmp(s, "RPS_seat") == 0) { out = Motion::RpsSeat; return true; }
  if (std::strcmp(s, "rotation_z") == 0) { out = Motion::RotationZ; return true; }
  if (std::strcmp(s, "z_transfer") == 0) { out = Motion::ZTransfer; return true; }
  if (std::strcmp(s, "compound") == 0) { out = Motion::Compound; return true; }
  return false;
}
inline bool isHeaderRow(const char *line) { return std::strncmp(line, "station_id,", 11) == 0; }
// Parses the whole of `s` as a float: rejects anything strtof did not
// consume entirely ("10xyz", "abc" -> 0 silently) and non-finite results
// ("nan", "inf"). An empty `s` means "unset" (an omitted optional trailing
// column) and parses as 0, not a failure -- callers that require the field
// check `s[0] != 0` themselves before calling this.
inline bool parseFloat(const char *s, float &out) {
  if (s[0] == 0) { out = 0; return true; }
  char *end = nullptr;
  const float v = std::strtof(s, &end);
  if (end == s || *end != 0 || !std::isfinite(v)) return false;
  out = v;
  return true;
}
// A non-zero whole multiple of 90 (degrees): every turn on the line is.
inline bool quarterTurns(float deg) {
  const float q = deg / 90;
  return deg != 0 && std::fabs(q - std::round(q)) < 1e-3f;
}

// Columns: station_id,direction,distance_m,wifi_scan,station_motion,
// motion_axis,motion_range. motion_axis is read and not used: every motion
// here is about or along Z, either way.
inline const char *parseRow(const char *line, StationRow &out) {
  const char *p = line;
  char field[16];
  StationRow r;

  detail::nextField(p, field, sizeof(field));
  if (field[0] == 0) return "pusty station_id";
  if (std::strlen(field) >= sizeof(r.id)) return "station_id dluzszy niz 11 znakow"; // Not cut short: names name log files.
  std::memcpy(r.id, field, std::strlen(field) + 1);

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseDirection(field, r.direction)) return "zly direction";

  detail::nextField(p, field, sizeof(field));
  if (field[0] == 0 || !detail::parseFloat(field, r.distanceM) || r.distanceM < 0) return "zly distance_m";

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseBool(field, r.wifiScan)) return "zly wifi_scan";

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseMotion(field, r.motion)) return "zly station_motion";

  detail::nextField(p, field, sizeof(field)); // motion_axis: not used.

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseFloat(field, r.motionRange)) return "zly motion_range";
  if (r.motion == Motion::RotationZ && !quarterTurns(r.motionRange)) return "rotation_z: kat nie jest wielokrotnoscia 90";
  if (r.motion != Motion::RotationZ && r.motionRange < 0) return "ujemny motion_range";

  if (*p != 0 && *p != '\n' && *p != '\r') return "za duzo kolumn";

  out = r;
  return nullptr;
}
} // namespace detail

// Parses the whole file text (rows separated by '\n', CRLF tolerated, an
// optional header row and a UTF-8 BOM are skipped). A spreadsheet's Polish
// export (';' between fields, ',' as the decimal point) is read too. All or
// nothing: one malformed or unrecognised row rejects the whole file, and
// `err` (when given) names it. Without a valid file the line is not tracked.
inline bool parseStationConfig(const char *text, StationConfig &out, ConfigError *err = nullptr) {
  const auto fail = [&](int line, const char *what) {
    if (err) { err->line = line; err->what = what; }
    return false;
  };
  StationConfig result;
  const char *p = text;
  if (std::strncmp(p, "\xEF\xBB\xBF", 3) == 0) p += 3;
  int lineNo = 0;
  while (*p) {
    const char *lineStart = p;
    while (*p && *p != '\n') ++p;
    const char *lineEnd = p;
    if (*p == '\n') ++p;
    ++lineNo;
    if (lineEnd > lineStart && lineEnd[-1] == '\r') --lineEnd;
    if (lineEnd == lineStart) continue; // blank line
    char buf[160];
    int n = int(lineEnd - lineStart);
    if (n >= (int)sizeof(buf)) return fail(lineNo, "wiersz za dlugi");
    std::memcpy(buf, lineStart, n);
    buf[n] = 0;
    if (std::strchr(buf, ';')) // ';' between fields: ',' is the decimal point.
      for (char *c = buf; *c; ++c) *c = *c == ',' ? '.' : *c == ';' ? ',' : *c;
    if (detail::isHeaderRow(buf)) continue;
    if (result.count >= StationConfig::maxRows) return fail(lineNo, "ponad 64 stacje");
    StationRow &row = result.row[result.count];
    if (const char *what = detail::parseRow(buf, row)) return fail(lineNo, what);
    if (result.count > 0) { // The stretch from the previous station.
      const Direction d = row.direction;
      const bool turn = d == Direction::RZPlus || d == Direction::RZMinus;
      if (turn && !detail::quarterTurns(row.distanceM)) return fail(lineNo, "RZ: kat nie jest wielokrotnoscia 90");
      if (!turn && row.distanceM <= 0) return fail(lineNo, "distance_m musi byc > 0");
    }
    ++result.count;
  }
  if (result.count < 2) return fail(0, result.count ? "tylko jedna stacja" : "brak stacji");
  out = result;
  return true;
}

} // namespace line
