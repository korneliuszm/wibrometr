// test/station_config_checks.cpp
// g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc
#include <cmath>
#include <cstdio>
#include <string>
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
    const char *text = "ST01,X+,8,false,true,none,,,\nST01d,Z-,1,false,false,none,,,\nST02,Y-,3,false,false,none,,,\n"
                       "ST02u,Z+,1,false,false,none,,,\n";
    check(parseStationConfig(text, cfg), "Z- / Z+ (lift shaft) directions parse");
    StationConfig rz;
    check(parseStationConfig("B1,X+,0,false,true,none,,,\nB1r,RZ+,90,false,false,none,,,\nB1l,RZ-,90,false,false,none,,,\n", rz) &&
          rz.row[1].direction == Direction::RZPlus && rz.row[2].direction == Direction::RZMinus,
          "RZ+ / RZ- (turntable) directions parse");
    check(cfg.count == 4 && cfg.row[1].direction == Direction::ZMinus && cfg.row[3].direction == Direction::ZPlus,
          "Z- and Z+ read as ZMinus / ZPlus");
  }
  {
    StationConfig cfg;
    std::string text;
    for (int i = 0; i < StationConfig::maxRows; ++i) text += "ST,X+,8,false,false,none,,,\n";
    check(StationConfig::maxRows == 64 && parseStationConfig(text.c_str(), cfg) && cfg.count == 64, "64 stations parse");
    text += "ST,X+,8,false,false,none,,,\n";
    check(!parseStationConfig(text.c_str(), cfg), "a 65th station rejects the file");
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
  {
    StationConfig cfg;
    const char *text = "ST01,X+,abc,false,true,none,,,\n";
    check(!parseStationConfig(text, cfg), "garbage distance_m (\"abc\") rejects the row, not silently 0");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10xyz,false,true,none,,,\n";
    check(!parseStationConfig(text, cfg), "trailing junk (\"10xyz\") rejects the row, not silently 10");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,nan,false,true,none,,,\n";
    check(!parseStationConfig(text, cfg), "\"nan\" distance_m is non-finite and rejected");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,lift,Z,inf,0.1\n";
    check(!parseStationConfig(text, cfg), "\"inf\" motion_range is non-finite and rejected");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,lift,Z,bogus,\n";
    check(!parseStationConfig(text, cfg), "garbage motion_range (\"bogus\") rejects the row");
  }
  {
    // Trailing optional columns (motion_axis, motion_range, motion_speed)
    // may be omitted entirely, not just left empty: documented behaviour
    // of StationRow (motionRange/motionSpeed default to 0, "unset").
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,none\n";
    check(parseStationConfig(text, cfg), "omitting all trailing optional columns is accepted");
    check(cfg.row[0].motionRange == 0 && cfg.row[0].motionSpeed == 0,
          "omitted trailing columns default to 0 (unset)");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
