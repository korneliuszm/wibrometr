// test/station_config_checks.cpp
// g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc
#include <cmath>
#include <cstdio>
#include <cstring>
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
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "ST01,X+,0,true,none,,\n"
      "ST02,X+,10,false,lift,Z,0.4\n"
      "ST03,Y+,8,true,rotation_z,Z,90\n"
      "ST04,X+,8,true,RPS_seat,Z,0.3\n";
    check(parseStationConfig(text, cfg), "well-formed 4-row file parses");
    check(cfg.count == 4, "four rows read");
    check(!cfg.row[1].wifiScan && cfg.row[2].wifiScan, "wifi_scan parsed");
    check(cfg.row[1].motion == Motion::Lift, "lift motion parsed");
    check(std::fabs(cfg.row[1].motionRange - 0.4f) < 1e-6f, "motion range parsed");
    check(cfg.row[2].direction == Direction::YPlus, "Y+ direction parsed");
    check(cfg.row[2].motion == Motion::RotationZ, "rotation_z motion parsed");
    check(std::fabs(cfg.row[2].motionRange - 90.0f) < 1e-6f, "rotation range in degrees parsed");
    check(cfg.row[3].motion == Motion::RpsSeat && std::fabs(cfg.row[3].motionRange - 0.3f) < 1e-6f, "RPS_seat parsed");
  }
  {
    StationConfig cfg;
    check(parseStationConfig("ST01,X+,0,true,lift,Z+,0.5\nST02,X+,8,true,RPS_seat,Z-/Z+,0.5\nST03,X+,8,true,lift,,0.5\n", cfg),
          "motion_axis is read and not used: Z, Z+, Z-/Z+ or empty");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,8,true,none,,\nST01d,Z-,1,false,none,,\nST02,Y-,3,false,none,,\nST02u,Z+,1,false,none,,\n";
    check(parseStationConfig(text, cfg) && cfg.count == 4 && cfg.row[1].direction == Direction::ZMinus &&
            cfg.row[3].direction == Direction::ZPlus,
          "Z- / Z+ (lift shaft) directions parse");
    StationConfig rz;
    check(parseStationConfig("B1,X+,0,true,none,,\nB1r,RZ+,90,false,none,,\nB1l,RZ-,180,false,none,,\n", rz) &&
            rz.row[1].direction == Direction::RZPlus && rz.row[2].direction == Direction::RZMinus,
          "RZ+ / RZ- (turntable) directions parse");
  }
  {
    // Every turn on the line is a multiple of 90 degrees.
    StationConfig cfg;
    ConfigError err;
    check(!parseStationConfig("B1,X+,0,true,none,,\nB1r,RZ+,45,false,none,,\n", cfg, &err) && err.line == 2,
          "RZ by 45 deg is rejected, at line 2");
    check(!parseStationConfig("B1,X+,0,true,rotation_z,Z,80\nB2,X+,6,true,none,,\n", cfg, &err) && err.line == 1,
          "rotation_z by 80 deg is rejected, at line 1");
    check(!parseStationConfig("B1,X+,0,true,rotation_z,Z,\nB2,X+,6,true,none,,\n", cfg, &err), "rotation_z without an angle is rejected");
    check(parseStationConfig("B1,X+,0,true,rotation_z,Z,-180\nB2,X+,6,true,none,,\n", cfg), "rotation_z by -180 deg is accepted");
  }
  {
    StationConfig cfg;
    std::string text;
    for (int i = 0; i < StationConfig::maxRows; ++i) text += "ST,X+,8,false,none,,\n";
    check(StationConfig::maxRows == 64 && parseStationConfig(text.c_str(), cfg) && cfg.count == 64, "64 stations parse");
    text += "ST,X+,8,false,none,,\n";
    ConfigError err;
    check(!parseStationConfig(text.c_str(), cfg, &err) && err.line == 65, "a 65th station rejects the file, at line 65");
  }
  {
    StationConfig cfg;
    ConfigError err;
    const char *text = "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
                       "ST01,X+,10,true,none,,\nST02,bogus,10,true,none,,\n";
    check(!parseStationConfig(text, cfg, &err) && err.line == 3 && std::strstr(err.what, "direction"),
          "unknown direction rejects the whole file, names line 3 and the column");
  }
  {
    StationConfig cfg;
    check(!parseStationConfig("ST01,X+,10,true,none,,\nST02,X+,10\n", cfg), "a missing wifi_scan rejects the whole file");
    check(!parseStationConfig("ST01,X+,10,false,true,none,,,\nST02,X+,10,false,true,none,,,\n", cfg),
          "the old 9-column layout (passthrough, motion_speed) is rejected");
  }
  {
    StationConfig cfg;
    ConfigError err;
    check(!parseStationConfig("", cfg, &err) && err.line == 0, "empty file is rejected");
    check(!parseStationConfig("ST01,X+,0,true,none,,\n", cfg, &err), "a single station is no line");
    check(!parseStationConfig("ST01,X+,0,true,none,,\nST02,X+,0,true,none,,\n", cfg, &err) && err.line == 2,
          "a gap of 0 m is rejected");
  }
  {
    StationConfig cfg;
    check(!parseStationConfig("ST01,X+,0,true,none,,\nST02,X+,abc,true,none,,\n", cfg), "garbage distance_m rejects the row");
    check(!parseStationConfig("ST01,X+,0,true,none,,\nST02,X+,10xyz,true,none,,\n", cfg), "trailing junk rejects the row");
    check(!parseStationConfig("ST01,X+,0,true,none,,\nST02,X+,nan,true,none,,\n", cfg), "\"nan\" distance_m is rejected");
    check(!parseStationConfig("ST01,X+,0,true,lift,Z,inf\nST02,X+,8,true,none,,\n", cfg), "\"inf\" motion_range is rejected");
    check(!parseStationConfig("ST01,X+,0,true,lift,Z,bogus\nST02,X+,8,true,none,,\n", cfg), "garbage motion_range is rejected");
  }
  {
    StationConfig cfg;
    ConfigError err;
    check(parseStationConfig("AUF120ABCDE,X+,0,true,none,,\nB2,X+,6,true,none,,\n", cfg) &&
            std::strcmp(cfg.row[0].id, "AUF120ABCDE") == 0, "an 11-character station_id is kept whole");
    check(!parseStationConfig("AUF120ABCDEF,X+,0,true,none,,\nB2,X+,6,true,none,,\n", cfg, &err) && err.line == 1,
          "a 12-character station_id is rejected, not cut short");
  }
  {
    // Trailing optional columns (motion_axis, motion_range) may be omitted.
    StationConfig cfg;
    check(parseStationConfig("ST01,X+,0,true,none\nST02,X+,8,true,none\n", cfg) && cfg.row[0].motionRange == 0,
          "omitting the trailing optional columns is accepted");
  }
  {
    // Saved by a spreadsheet: UTF-8 BOM, CRLF, Polish ';' and decimal ','.
    StationConfig cfg;
    const char *text = "\xEF\xBB\xBFstation_id;direction;distance_m;wifi_scan;station_motion;motion_axis;motion_range\r\n"
                       "B1;X+;0;true;RPS_seat;Z;0,45\r\nB2;X+;6,5;false;none;;\r\n";
    check(parseStationConfig(text, cfg) && cfg.count == 2 && std::fabs(cfg.row[1].distanceM - 6.5f) < 1e-6f &&
            std::fabs(cfg.row[0].motionRange - 0.45f) < 1e-6f,
          "BOM, CRLF, ';' and decimal ',' (a Polish spreadsheet's CSV) parse");
    check(parseStationConfig("\xEF\xBB\xBFstation_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
                             "B1,X+,0,true,none,,\nB2,X+,6.5,true,none,,\n", cfg) && cfg.count == 2,
          "BOM before a ',' header is skipped");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
