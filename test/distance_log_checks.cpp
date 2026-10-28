// test/distance_log_checks.cpp
// g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc && ./dlc
#include <cstdio>
#include <cstring>
#include "DistanceLog.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}

int main() {
  using namespace line;

  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 1234, "transport", 0, 1, "", Motion::None, "", 10.2f, 0, 0.9f, "");
    check(std::strstr(buf, "1234,transport,0,1,,,,10.200,,0.90,") != nullptr,
          "transport row has from/to, no station_id/motion/angle");
  }
  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 5678, "technological", -1, -1, "ST02", Motion::Lift, "Z", 0.38f, 0, 1.0f, "");
    check(std::strstr(buf, "5678,technological,,,ST02,lift,Z,0.380,,1.00,") != nullptr,
          "technological row has station_id/motion/axis, no from/to");
  }
  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 9, "technological", -1, -1, "ST03", Motion::RotationZ, "Z", 0, 91.0f, 1.0f, "over_range");
    check(std::strstr(buf, ",,91.000,1.00,over_range\n") != nullptr,
          "rotation row carries angle not distance, and the quality flag");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
