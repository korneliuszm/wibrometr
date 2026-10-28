#include "WifiScan.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "WebLink.h"

namespace wifiscan {
namespace {
constexpr int scansPerPrint = 3;
constexpr uint32_t msPerChannel = 110; // ~1.5 s per scan over 13 channels.
constexpr int maxSeen = 48;
struct Seen { uint8_t bssid[6]; float sum; int hits; };
Seen seen[maxSeen];
int seenCount = 0, scansDone = 0;
bool running = false, ready = false;
line::Fingerprint result;
struct Name { uint8_t bssid[6]; char ssid[33]; };
Name names[64];
int nameCount = 0, nameNext = 0;

void remember(const uint8_t *bssid, const char *ssid) {
  for (int i = 0; i < nameCount; ++i)
    if (std::memcmp(names[i].bssid, bssid, 6) == 0) { strncpy(names[i].ssid, ssid, 32); return; }
  Name &n = names[nameCount < 64 ? nameCount++ : (nameNext++ % 64)];
  std::memcpy(n.bssid, bssid, 6);
  strncpy(n.ssid, ssid, 32);
  n.ssid[32] = 0;
}
void collect(int found) {
  for (int i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid == weblink::ssid) continue; // Never our own access point.
    const uint8_t *b = WiFi.BSSID(i);
    if (!b) continue;
    remember(b, ssid.c_str());
    int k = 0;
    while (k < seenCount && std::memcmp(seen[k].bssid, b, 6) != 0) ++k;
    if (k == seenCount) {
      if (seenCount == maxSeen) continue;
      std::memcpy(seen[k].bssid, b, 6);
      seen[k].sum = 0;
      seen[k].hits = 0;
      ++seenCount;
    }
    seen[k].sum += WiFi.RSSI(i);
    ++seen[k].hits;
  }
  WiFi.scanDelete();
}
void finish() {
  line::Fingerprint f;
  Seen order[maxSeen];
  std::memcpy(order, seen, sizeof(Seen) * seenCount);
  const auto avg = [](const Seen &s) { return (s.sum + line::missingDbm * (scansPerPrint - s.hits)) / scansPerPrint; };
  for (int i = 1; i < seenCount; ++i) // Strongest first.
    for (int j = i; j > 0 && avg(order[j]) > avg(order[j - 1]); --j) std::swap(order[j], order[j - 1]);
  for (int i = 0; i < seenCount && f.count < line::maxAps; ++i) {
    std::memcpy(f.ap[f.count].bssid, order[i].bssid, 6);
    f.ap[f.count++].rssi = avg(order[i]);
  }
  result = f;
  ready = true;
  running = false;
}
} // namespace

void start() {
  if (running) return;
  seenCount = scansDone = 0;
  running = true;
  ready = false;
  WiFi.scanNetworks(true, false, false, msPerChannel);
}

void poll() {
  if (!running) return;
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n >= 0) collect(n); else WiFi.scanDelete();
  if (++scansDone < scansPerPrint) WiFi.scanNetworks(true, false, false, msPerChannel);
  else finish();
}

bool busy() { return running; }
bool take(line::Fingerprint &out) {
  if (!ready) return false;
  ready = false;
  out = result;
  return true;
}
const char *ssidOf(const uint8_t *bssid) {
  for (int i = 0; i < nameCount; ++i)
    if (std::memcmp(names[i].bssid, bssid, 6) == 0) return names[i].ssid;
  return "";
}
} // namespace wifiscan
