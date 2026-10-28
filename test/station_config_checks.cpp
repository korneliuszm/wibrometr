// test/station_config_checks.cpp
// g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc
#include <cmath>
#include <cstdio>
#include "StationConfig.h"

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
    StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed\n"
      "ST01,X+,10,false,true,none,,,\n"
      "ST02,X+,10,true,false,lift,Z,0.4,0.1\n"
      "ST03,Y+,8,false,true,rotation_z,Z,90,\n";
    check(parseStationConfig(text, cfg), "well-formed 3-row file parses");
    check(cfg.count == 3, "three rows read");
    check(cfg.row[1].passthrough == true, "passthrough true parsed");
    check(cfg.row[1].motion == Motion::Lift, "lift motion parsed");
    check(std::fabs(cfg.row[1].motionRange - 0.4f) < 1e-6f, "motion range parsed");
    check(cfg.row[2].direction == Direction::YPlus, "Y+ direction parsed");
    check(cfg.row[2].motion == Motion::RotationZ, "rotation_z motion parsed");
    check(std::fabs(cfg.row[2].motionRange - 90.0f) < 1e-6f, "rotation range in degrees parsed");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,none,,,\nST02,bogus,10,false,true,none,,,\n";
    check(!parseStationConfig(text, cfg), "unknown direction rejects the whole file");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,none,,,\nST02,X+,10,false\n"; // missing columns
    check(!parseStationConfig(text, cfg), "missing columns rejects the whole file");
  }
  {
    StationConfig cfg;
    check(!parseStationConfig("", cfg), "empty file is rejected (no fallback silently applied)");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
