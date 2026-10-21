#include "WifiScanner.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

namespace ips {

void WifiScanner::begin() {
  if (started_) return;
  WiFi.persistent(false);   // Never write Wi-Fi settings to flash.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  started_ = true;
}

bool WifiScanner::start() {
  if (!started_) begin();
  if (running_) return true;
  // async, include hidden networks (they still have a BSSID), active scan.
  const int16_t r = WiFi.scanNetworks(true, true, false, kScanMsPerChannel);
  if (r != WIFI_SCAN_RUNNING && r < 0) return false;
  running_ = true;
  startMs_ = millis();
  return true;
}

WifiScanner::State WifiScanner::poll(ScanResult &out) {
  if (!running_) return State::Idle;
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return State::Running;
  running_ = false;
  if (n < 0) {
    WiFi.scanDelete();
    return State::Failed;
  }
  out = ScanResult{};
  out.startMs = startMs_;
  out.durationMs = millis() - startMs_;
  // Results come strongest first; keep what fits.
  for (int16_t i = 0; i < n && out.count < kMaxScanAps; ++i) {
    ApReading r;
    const uint8_t *b = WiFi.BSSID(i);
    if (!b) continue;
    std::memcpy(r.bssid.b, b, 6);
    const int32_t rssi = WiFi.RSSI(i);
    r.rssi = int8_t(rssi < -127 ? -127 : rssi > 0 ? 0 : rssi);
    r.channel = uint8_t(WiFi.channel(i));
    const String ssid = WiFi.SSID(i);
    std::strncpy(r.ssid, ssid.c_str(), kSsidLen - 1);
    out.add(r);
  }
  WiFi.scanDelete();
  return State::Done;
}

} // namespace ips
