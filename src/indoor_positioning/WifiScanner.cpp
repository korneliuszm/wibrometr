#include "WifiScanner.h"
#include <Arduino.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <atomic>
#include <cstring>

namespace ips {

namespace {
constexpr uint32_t kScanTimeoutMs = 10000; // A scan never takes this long.
std::atomic<bool> scanDone{false};
wifi_ap_record_t records[kMaxScanAps]; // ~4 KB, static: no heap per scan.

// Runs in the ESP-IDF event task; only raises a flag for poll().
void onScanDone(void *, esp_event_base_t, int32_t, void *) { scanDone = true; }
} // namespace

bool WifiScanner::begin() {
  if (started_) return true;
  // Same bring-up order as the Arduino WiFi library: netif, event loop, driver.
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) {
    Serial.printf("IPS: esp_netif_init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { // INVALID_STATE: already exists.
    Serial.printf("IPS: event loop failed: %s\n", esp_err_to_name(err));
    return false;
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0; // Never write Wi-Fi settings to flash.
  err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("IPS: esp_wifi_init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &onScanDone, nullptr);
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) err = esp_wifi_start();
  if (err != ESP_OK) {
    Serial.printf("IPS: Wi-Fi start failed: %s\n", esp_err_to_name(err));
    return false;
  }
  started_ = true;
  return true;
}

bool WifiScanner::start() {
  if (running_) return true;
  if (!started_ && !begin()) return false;
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = true; // Hidden networks still have a usable BSSID.
  cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  cfg.scan_time.active.min = 0;
  cfg.scan_time.active.max = kScanMsPerChannel;
  scanDone = false;
  if (esp_wifi_scan_start(&cfg, false) != ESP_OK) return false; // false: do not block.
  running_ = true;
  startMs_ = millis();
  return true;
}

WifiScanner::State WifiScanner::poll(ScanResult &out) {
  if (!running_) return State::Idle;
  if (!scanDone) {
    if (millis() - startMs_ < kScanTimeoutMs) return State::Running;
    esp_wifi_scan_stop();
    running_ = false;
    return State::Failed;
  }
  running_ = false;
  uint16_t n = kMaxScanAps;
  // Also frees the driver's result list.
  if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
    esp_wifi_clear_ap_list();
    return State::Failed;
  }
  esp_wifi_clear_ap_list(); // Drops any results beyond kMaxScanAps.
  out = ScanResult{};
  out.startMs = startMs_;
  out.durationMs = millis() - startMs_;
  for (uint16_t i = 0; i < n; ++i) {
    ApReading r;
    std::memcpy(r.bssid.b, records[i].bssid, 6);
    r.rssi = records[i].rssi;
    r.channel = records[i].primary;
    std::strncpy(r.ssid, reinterpret_cast<const char *>(records[i].ssid), kSsidLen - 1);
    out.add(r);
  }
  return State::Done;
}

} // namespace ips
