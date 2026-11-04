#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Text formats of the SD card logs; no file or Arduino access, tested on a PC.
//
// One file per recording session: /LOGS/S0001.CSV, S0002.CSV, ... A short
// header of '#' comment lines carries the session parameters (including the
// hanger tag, once known), then one CSV line per sample:
//   t_us,ax,ay,az      time since boot in microseconds, acceleration in m/s2
// Lines starting with '#' inside the data are events (battery, tag changes).
namespace logfmt {

constexpr const char *kDirectory = "/LOGS";
constexpr std::size_t kMaxTagLen = 8;

struct Header {
  unsigned session = 0;
  uint32_t uptimeMs = 0;   // Device clock at the start (no RTC on the board).
  float sampleRateHz = 0;
  float rangeG = 0;
  char tag[kMaxTagLen + 1] = ""; // Hanger tag number, empty if unknown.
};

// "/LOGS/S0007.CSV"
inline std::size_t sessionPath(unsigned session, char *out, std::size_t size) {
  const int n = std::snprintf(out, size, "%s/S%04u.CSV", kDirectory, session % 10000);
  return n < 0 ? 0 : std::size_t(n) < size ? std::size_t(n) : size - 1;
}

// Accepts "S0007.CSV" (any case, with or without a leading directory).
inline bool parseSessionNumber(const char *name, unsigned &session) {
  if (!name) return false;
  if (const char *slash = std::strrchr(name, '/')) name = slash + 1;
  if ((name[0] != 'S' && name[0] != 's') || std::strlen(name) != 9) return false;
  unsigned value = 0;
  for (int i = 1; i <= 4; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
    value = value * 10 + unsigned(name[i] - '0');
  }
  const char *ext = name + 5;
  if (ext[0] != '.' || (ext[1] != 'C' && ext[1] != 'c') || (ext[2] != 'S' && ext[2] != 's') ||
      (ext[3] != 'V' && ext[3] != 'v'))
    return false;
  session = value;
  return true;
}

// Next free session number after the highest existing one; wraps after 9999.
constexpr unsigned nextSession(unsigned highestExisting) {
  return highestExisting >= 9999 ? 1 : highestExisting + 1;
}

// Keeps only digits, so a tag can never break the header line.
inline void setTag(Header &h, const char *digits) {
  std::size_t n = 0;
  for (const char *p = digits; p && *p && n < kMaxTagLen; ++p)
    if (*p >= '0' && *p <= '9') h.tag[n++] = *p;
  h.tag[n] = '\0';
}

inline std::size_t formatHeader(char *out, std::size_t size, const Header &h) {
  const int n = std::snprintf(out, size,
                              "# wibrometr log v1\n# device: ESP32-S3-Touch-LCD-2, QMI8658\n"
                              "# session: %u\n# uptime_ms_at_start: %lu\n# sample_rate_hz: %.1f\n"
                              "# range_g: %.0f\n# tag: %s\n# columns: t_us,ax_m_s2,ay_m_s2,az_m_s2\n",
                              h.session, (unsigned long)h.uptimeMs, h.sampleRateHz, h.rangeG, h.tag);
  return n < 0 ? 0 : std::size_t(n) < size ? std::size_t(n) : size - 1;
}

// Fixed point with three decimals, without printf's floating point cost.
inline std::size_t formatFixed3(char *out, std::size_t size, float value) {
  if (size < 12) return 0;
  const bool negative = value < 0;
  float a = negative ? -value : value;
  if (a > 99999.0f) a = 99999.0f; // Far beyond any sensor range.
  const uint32_t milli = uint32_t(a * 1000.0f + 0.5f);
  const int n = std::snprintf(out, size, "%s%lu.%03lu", negative && milli ? "-" : "",
                              (unsigned long)(milli / 1000), (unsigned long)(milli % 1000));
  return n < 0 ? 0 : std::size_t(n);
}

inline std::size_t formatSample(char *out, std::size_t size, uint32_t tUs, float ax, float ay, float az) {
  if (size < 64) return 0;
  std::size_t n = std::size_t(std::snprintf(out, size, "%lu,", (unsigned long)tUs));
  n += formatFixed3(out + n, size - n, ax);
  out[n++] = ',';
  n += formatFixed3(out + n, size - n, ay);
  out[n++] = ',';
  n += formatFixed3(out + n, size - n, az);
  out[n++] = '\n';
  out[n] = '\0'; // Not counted in the result; keeps the buffer a valid C string.
  return n;
}

inline std::size_t formatBatteryEvent(char *out, std::size_t size, uint32_t tUs, float millivolts,
                                      bool charging) {
  const int n = std::snprintf(out, size, "# battery t_us=%lu mV=%.0f charging=%d\n", (unsigned long)tUs,
                              millivolts, charging ? 1 : 0);
  return n < 0 ? 0 : std::size_t(n) < size ? std::size_t(n) : size - 1;
}

} // namespace logfmt
