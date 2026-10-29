#pragma once
#include <cstdint>
#include "Displacement.h"
#include "LineTracker.h"

// Wi-Fi access point with a web page that plots the sensor live.
// The page (src/WebPage.h) opens a WebSocket at /ws. Every 50 ms the board
// sends one JSON batch with all new samples (accel m/s2, gyro deg/s without
// its offset), the displacement meter and the line state. It also sends the
// last move and the line (tables, spacings, Wi-Fi fingerprints) when it
// changes; a new page gets both.
// All calls come from the Arduino loop task; the server runs on its own task.
namespace weblink {
constexpr const char *ssid = "Wibrometr";
constexpr const char *password = "wibrometr"; // WPA2 needs at least 8 characters.

// Requests from a page, handled by the loop task.
enum class Command : uint8_t { None, ResetTotals, Home, ForgetWifi };

void begin();
// One sample, in arrival order. `us` is the sample time.
void addSample(const Accel &accel, const Accel &gyroDps, uint32_t us);
// Sends the queued samples and state when due; `rateHz` is the measured rate.
void loop(uint32_t now, const DisplacementMeter &meter, const line::Tracker &tracker, float rateHz, bool live,
          bool scanning);
void publishMove(const DisplacementMeter &meter);
void publishLine(const line::Tracker &tracker);
// Without a valid config.csv the page says why (line 0: the file as a whole).
void setConfigProblem(int line, const char *what);
// The next request from a page.
Command takeCommand();
int clients();
const char *address(); // "192.168.4.1"
} // namespace weblink
