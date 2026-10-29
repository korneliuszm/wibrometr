#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <cerrno>
namespace storage {
inline constexpr char logHeader[] =
  "time,timestamp_ms,segment_type,from_station,to_station,station_id,motion_type,"
  "axis,measured_distance_m,measured_angle_deg,confidence,quality_flags,declared,delta,"
  "duration_s,ramp_m_s2,rms_x,rms_y,rms_z,peak_x,peak_y,peak_z\n";
// Rotate to the first free backup name; never delete an older backup.
// A failed rename leaves the old-format source untouched and does not append.
inline bool appendCsvFile(const char *path, const char *row) {
  bool fresh = true;
  FILE *f = fopen(path, "r");
  if (f) {
    char first[sizeof(logHeader) + 2]{};
    const bool same = fgets(first, sizeof first, f) && !strcmp(first, logHeader);
    const bool error = ferror(f);
    fclose(f);
    if (error) return false;
    fresh = false;
    if (!same) {
      const size_t len = strlen(path);
      if (len < 4) return false;
      const std::string stem(path, len - 4);
      std::string backup;
      bool found = false;
      for (unsigned i = 1; i <= 9999; ++i) {
        backup = stem + "_old" + (i == 1 ? "" : std::to_string(i)) + ".csv";
        struct stat st{};
        if (stat(backup.c_str(), &st) != 0) {
          if (errno != ENOENT) return false;
          found = true;
          break;
        }
      }
      if (!found || rename(path, backup.c_str()) != 0) return false;
      fresh = true;
    }
  } else if (errno != ENOENT) return false;
  f = fopen(path, "a");
  if (!f) return false;
  bool ok = !fresh || fputs(logHeader, f) >= 0;
  if (ok) ok = fputs(row, f) >= 0;
  if (fclose(f) != 0) ok = false;
  return ok;
}
}
