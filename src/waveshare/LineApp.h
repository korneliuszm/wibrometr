#pragma once
#include <cstdint>
#include "ClientNetwork.h"
#include "Decimator.h"
#include "Displacement.h"
#include "ImuTask.h"
#include "LineTracker.h"
#include "hal/SdLogger.h"

// The roller-line product on the Waveshare board: position on the line from the
// ride (line::Tracker), rest-to-rest displacement, the Wi-Fi/web dashboard,
// config.csv on the USB drive, learned data in NVS. Same behaviour as the
// Feather firmware; only the sensor, the screens and the input differ.
// Everything here runs on the Arduino loop task.
namespace lineapp {

struct Gyro {
  Accel shown{};   // Smoothed rate without the rest offset, deg/s.
  Accel angle{};   // Turned since zeroing, deg.
};

// Reads NVS and config.csv, starts Wi-Fi, the web server and USB storage.
void begin(hal::SdLogger *sd);
// One IMU sample at the sensor rate (500 Hz). Decimates for the line logic.
void onSample(const ImuEvent &e);
// Sensor lost or reconnecting: every filter restarts.
void onGap();
// Network, Wi-Fi scans, finished transits, logs, web commands. `live`: samples
// are arriving. `rateHz`: measured sensor rate.
void service(uint32_t now, bool live, float rateHz);
// Short text for the status line after something happened, once; else null.
const char *takeNotice();

line::Tracker &tracker();
DisplacementMeter &displacement();
bool configOk();
const line::ConfigError &configError();
const Gyro &gyro();

// Actions (also reachable from the web page).
void home();           // Part is on table 1.
void resetTotals();    // Displacement sums.
void zeroGyroAngle();

} // namespace lineapp
