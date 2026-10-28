#pragma once
#include <cstdint>
#include "Displacement.h"
#include "LineTracker.h"

// Wi-Fi access point with a web page that plots the sensor live.
// The page (src/WebPage.h) opens a WebSocket at /ws. Every 50 ms the board
// sends one JSON batch with all new samples (accel m/s2, gyro deg/s without
// its offset), the displacement meter and the line state. It also sends the
// last move, the line (tables, spacings, Wi-Fi fingerprints) when it changes
// and every finished transit with its vibration; a new page gets all of them.
// All calls come from the Arduino loop task; the server runs on its own task.
namespace weblink {
constexpr const char *ssid = "Wibrometr";
constexpr const char *password = "wibrometr"; // WPA2 needs at least 8 characters.

// Requests from a page, handled by the loop task.
enum class Command : uint8_t { None, ResetTotals, Home, Stations, ForgetWifi, ForgetAll };

void begin();
// One sample, in arrival order. `us` is the sample time.
void addSample(const Accel &accel, const Accel &gyroDps, uint32_t us);
// Sends the queued samples and state when due; `rateHz` is the measured rate.
void loop(uint32_t now, const DisplacementMeter &meter, const line::Tracker &tracker, float rateHz, bool live,
          bool scanning);
void publishMove(const DisplacementMeter &meter);
void publishLine(const line::Tracker &tracker);
void publishTransit(const line::Transit &transit);
// The next request from a page; `arg` carries the table count for Stations.
Command takeCommand(int &arg);
int clients();
const char *address(); // "192.168.4.1"
} // namespace weblink
