// test/distance_log_checks.cpp
// g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc && ./dlc
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
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
    check(std::strstr(buf, ",,91.000,1.00,over_range,,\n") != nullptr,
          "rotation row carries angle not distance, and the quality flag");
  }

  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 7, "technological", -1, -1, "B3", Motion::RpsSeat, "Z", 0.48f, 0, 1.0f, "back_m=0.470", 0.5f);
    check(std::strstr(buf, ",B3,RPS_seat,Z,0.480,,1.00,back_m=0.470,0.500,-0.020\n") != nullptr,
          "RPS_seat row: the way out, back_m, declared and delta");
  }
  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 1, "transport", 1, 2, "", Motion::None, "", 6.9f, 0, 1.0f, "", 6.5f);
    check(std::strstr(buf, ",6.900,,1.00,,6.500,0.400\n") != nullptr, "declared distance and delta (measured - declared)");
    formatDistanceRow(buf, sizeof(buf), 1, "technological", -1, -1, "B1", Motion::RotationZ, "Z", 0, -88.0f, 1.0f, "", -90.0f);
    check(std::strstr(buf, ",-88.000,1.00,,-90.000,2.000\n") != nullptr, "declared turn is signed like the measured one");
  }
  {
    char buf[256];
    formatDistanceRow(buf, sizeof(buf), 1, "transport", 1, 2, "", Motion::None, "", 6.5f, 0, 1.0f, "");
    const float rms[3] = {0.1f, 0.2f, 0.3f}, peak[3] = {1, 2, 3};
    appendRideColumns(buf, sizeof(buf), 12.5f, 0.42f, rms, peak);
    check(std::strstr(buf, ",1.00,,,,12.50,0.420,0.100,0.200,0.300,1.000,2.000,3.000\n") != nullptr,
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

  {
    setenv("TZ", "UTC0", 1); tzset();
    char time[32], csv[288];
    check(formatEventTime(time, sizeof time, 1735689600123LL, 10000, 8500) &&
          !strcmp(time,"2024-12-31 23:59:58.623"), "event time accounts for queue age and midnight");
    check(prependLogTime(csv,sizeof csv,"1234,transport,...\n",time) &&
          !strcmp(csv,"2024-12-31 23:59:58.623,1234,transport,...\n"),"time column preserves CSV");
    for (int64_t epoch : {int64_t(0),int64_t(1704067200000LL)}) {
      check(formatEventTime(time,sizeof time,epoch,10000,8500) && !time[0],"unset time empty");
      check(prependLogTime(csv,sizeof csv,"1234,transport,...\n",time) && csv[0]==',',"empty first column");
    }
    check(formatEventTime(time,sizeof time,1704067200001LL,0,0) && time[0],"time above threshold");
    check(formatEventTime(time,sizeof time,1735689600123LL,0x100,0xffffff00) &&
          !strcmp(time,"2024-12-31 23:59:59.611"),"uint32 wrap age");
    char tiny[3]={'x','x','x'};
    check(!prependLogTime(tiny,sizeof tiny,"1234\n","t") && tiny[0]==0,"reject truncated CSV");
    check(!formatEventTime(tiny,sizeof tiny,1735689600123LL,0,0) && tiny[0]==0,"reject truncated time");
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
    std::tm utc{}; utc.tm_year=126; utc.tm_mon=9; utc.tm_mday=25; utc.tm_hour=1;
    const int64_t transition=int64_t(timegm(&utc))*1000;
    check(formatEventTime(time,sizeof time,transition,1000,0) && !strcmp(time,"2026-10-25 02:59:59.000"),"DST before transition");
    check(formatEventTime(time,sizeof time,transition,1000,1000) && !strcmp(time,"2026-10-25 02:00:00.000"),"DST after transition");
    setenv("TZ","UTC0",1); tzset();
    for (uint32_t event : {8500u,9000u,10000u}) {
      char raw[256]; memset(raw,'a',sizeof raw); snprintf(raw,32,"%u,transport,",event);
      size_t n=strlen(raw); memset(raw+n,'a',254-n); raw[254]='\n';raw[255]=0;
      check(formatEventTime(time,sizeof time,1735689600123LL,10000,event) && prependLogTime(csv,sizeof csv,raw,time) &&
            !strcmp(strchr(csv,',')+1,raw) && strlen(csv)==279,"batch age and maximum raw CSV preserved");
    }
    check(formatEventTime(time,sizeof time,1735689601123LL,10000,10000) && !strcmp(time,"2025-01-01 00:00:01.123"),"fresh epoch each batch");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
