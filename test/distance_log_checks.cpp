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

  {
    char buf[256];
    formatDistanceRow(buf, sizeof(buf), 1, "transport", 1, 2, "", Motion::None, "", 6.5f, 0, 1.0f, "");
    const float rms[3] = {0.1f, 0.2f, 0.3f}, peak[3] = {1, 2, 3};
    appendRideColumns(buf, sizeof(buf), 12.5f, 0.42f, rms, peak);
    check(std::strstr(buf, ",1.00,,12.50,0.420,0.100,0.200,0.300,1.000,2.000,3.000\n") != nullptr,
          "ride columns follow the quality flags, one newline at the end");
  }
  {
    const auto name = [](int s) { return s < 0 ? "nieznana" : s == 1 ? "B2" : s == 2 ? "B3" : "A/B?"; };
    char out[32];
    logFileName(out, sizeof out, false, 1, 2, name);
    check(std::strcmp(out, "B2-B3") == 0, "ride file named from-to: B2-B3");
    logFileName(out, sizeof out, true, 2, 2, name);
    check(std::strcmp(out, "B3_tech") == 0, "technological motion file: B3_tech");
    logFileName(out, sizeof out, false, -1, 2, name);
    check(std::strcmp(out, "nieznana-B3") == 0, "unknown start station in the name");
    logFileName(out, sizeof out, true, 5, 5, name);
    check(std::strcmp(out, "A_B__tech") == 0, "characters FAT does not allow become '_'");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
