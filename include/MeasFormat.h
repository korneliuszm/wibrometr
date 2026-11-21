#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "LogFormat.h"

// Text formats of the vibration recorder's measurement files on the SD card;
// no file or Arduino access, tested on a PC.
//
// One file per measurement: /MEAS/M0001.CSV, M0002.CSV, ... A header of '#'
// comment lines carries the parameters, then a plain column-names line (so
// pandas comment='#' and spreadsheets get column names), then one CSV line per
// sample:
//   t_ms,ax,ay,az      time relative to the trigger (negative = pre-trigger)
// The file ends with a '#' footer line with sample and drop counts.
namespace measfmt {

constexpr const char *kDirectory = "/MEAS";

struct Header {
  unsigned number = 0;
  int thresholdDeci = 0; // Trigger threshold in 0.1 m/s2.
  float sampleRateHz = 0;
  float rangeG = 0;
  float preRollS = 0;
  float quietS = 0;
};

inline std::size_t clampLen(int n, std::size_t size) {
  return n < 0 ? 0 : std::size_t(n) < size ? std::size_t(n) : size - 1;
}

// "/MEAS/M0007.CSV"
inline std::size_t measPath(unsigned number, char *out, std::size_t size) {
  return clampLen(std::snprintf(out, size, "%s/M%04u.CSV", kDirectory, number % 10000), size);
}

// Accepts "M0007.CSV" (any case, with or without a leading directory).
inline bool parseMeasNumber(const char *name, unsigned &number) {
  if (!name) return false;
  if (const char *slash = std::strrchr(name, '/')) name = slash + 1;
  if ((name[0] != 'M' && name[0] != 'm') || std::strlen(name) != 9) return false;
  unsigned value = 0;
  for (int i = 1; i <= 4; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
    value = value * 10 + unsigned(name[i] - '0');
  }
  const char *ext = name + 5;
  if (ext[0] != '.' || (ext[1] != 'C' && ext[1] != 'c') || (ext[2] != 'S' && ext[2] != 's') ||
      (ext[3] != 'V' && ext[3] != 'v'))
    return false;
  number = value;
  return true;
}

// Next measurement number; 0 means the numbering is exhausted (after 9999).
constexpr unsigned nextMeasNumber(unsigned highestExisting) {
  return highestExisting >= 9999 ? 0 : highestExisting + 1;
}

inline std::size_t formatHeader(char *out, std::size_t size, const Header &h) {
  const int thr = h.thresholdDeci;
  const unsigned thrAbs = unsigned(thr < 0 ? -thr : thr);
  return clampLen(std::snprintf(out, size,
                                "# wibrometr measurement v1\n# device: ESP32-S3-Touch-LCD-2, QMI8658\n"
                                "# measurement: %u\n# threshold_m_s2: %s%u.%u\n# sample_rate_hz: %.1f\n"
                                "# range_g: %.0f\n# pre_trigger_s: %.1f\n# quiet_s: %.1f\n"
                                "# trigger_at: t_ms = 0\n# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2\n"
                                "t_ms,ax_m_s2,ay_m_s2,az_m_s2\n",
                                h.number, thr < 0 ? "-" : "", thrAbs / 10, thrAbs % 10, h.sampleRateHz,
                                h.rangeG, h.preRollS, h.quietS),
                  size);
}

// "<ms>.<3 digits>,ax,ay,az\n"; time is relative to the trigger, in microseconds.
inline std::size_t formatSample(char *out, std::size_t size, int32_t relUs, float ax, float ay,
                                float az) {
  if (size < 64) return 0;
  const int64_t us = relUs;
  const uint32_t a = uint32_t(us < 0 ? -us : us);
  std::size_t n = std::size_t(std::snprintf(out, size, "%s%lu.%03lu,", us < 0 ? "-" : "",
                                            (unsigned long)(a / 1000), (unsigned long)(a % 1000)));
  n += logfmt::formatFixed3(out + n, size - n, ax);
  out[n++] = ',';
  n += logfmt::formatFixed3(out + n, size - n, ay);
  out[n++] = ',';
  n += logfmt::formatFixed3(out + n, size - n, az);
  out[n++] = '\n';
  out[n] = '\0'; // Not counted in the result; keeps the buffer a valid C string.
  return n;
}

inline std::size_t formatFooter(char *out, std::size_t size, uint32_t samples, uint32_t dropped) {
  return clampLen(std::snprintf(out, size, "# end: samples=%lu dropped=%lu\n", (unsigned long)samples,
                                (unsigned long)dropped),
                  size);
}

} // namespace measfmt
