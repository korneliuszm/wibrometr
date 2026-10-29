#pragma once
#include "Arduino.h"
constexpr int WIFI_SCAN_RUNNING=-1, WIFI_SCAN_FAILED=-2;
struct FakeWifi {
 int completion=WIFI_SCAN_FAILED, starts=0, deleted=0;
 int scanNetworks(bool,bool,bool,uint32_t) { ++starts; completion=WIFI_SCAN_RUNNING; return WIFI_SCAN_RUNNING; }
 int scanComplete() { return completion; }
 void scanDelete() { ++deleted; completion=WIFI_SCAN_FAILED; }
 String SSID(int) { return "xiaomi"; }
 const uint8_t *BSSID(int) { static const uint8_t b[]={1,2,3,4,5,6};return b; }
 float RSSI(int) { return -45; }
};
inline FakeWifi WiFi;
