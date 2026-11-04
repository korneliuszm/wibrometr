#include "ClientNetwork.h"
#include "WifiConfig.h"
#include "NetworkFiles.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>
#include <esp_eap_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

namespace net {
namespace {
Status current;
WifiConfig config;
std::string caPem, clientPem, keyPem; // Persistent buffers, also used on reconnect.
bool configured = false, mdns = false, started = false;
uint32_t lastRetry = 0;
struct Event { int type; int reason; };
QueueHandle_t events = nullptr;
void eventCallback(WiFiEvent_t type, WiFiEventInfo_t info) {
  if (!events) return;
  if (type != ARDUINO_EVENT_WIFI_STA_GOT_IP && type != ARDUINO_EVENT_WIFI_STA_DISCONNECTED &&
      type != ARDUINO_EVENT_WIFI_STA_LOST_IP) return;
  const Event event{int(type), type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED ? int(info.wifi_sta_disconnected.reason) : 0};
  xQueueSend(events, &event, 0);
}
void problem(int line, const char *why, const char *file = "wifi.txt") {
  current.state = State::ConfigError;
  current.errorLine = line;
  snprintf(current.message, sizeof current.message, "%.90s: %.60s", file, why);
  Serial.printf("network: blad wiersz %d: %s\n", line, current.message);
}
bool loadPem(const std::string &file, int line, bool key, std::string &out) {
  if (file.empty()) return true;
  const std::string path = "/ffat/" + file;
  const char *why = "";
  if (!readNetworkFile(path.c_str(), 8192, out, why)) { problem(line, why, file.c_str()); return false; }
  if (!pemEnvelopeValid(out, key)) { problem(line, key ? "bledny PEM lub zaszyfrowany klucz" : "bledny PEM", file.c_str()); return false; }
  return true;
}
const char *optional(const std::string &s) { return s.empty() ? nullptr : s.c_str(); }
void lostConnection(int reason) {
  if (mdns) { MDNS.end(); mdns = false; }
  current.state = State::Disconnected;
  current.ip[0] = 0;
  if (reason) current.disconnectReason = reason;
  snprintf(current.message, sizeof current.message, "rozlaczono (powod %d)", current.disconnectReason);
  lastRetry = millis();
  Serial.printf("network: %s\n", current.message);
}
void gotIp(const String &ip) {
  if (mdns) MDNS.end();
  mdns = MDNS.begin(config.hostname.c_str());
  if (mdns) MDNS.addService("http", "tcp", 80);
  else Serial.println("network: mDNS start failed");
  esp_sntp_set_time_sync_notification_cb([](struct timeval *) {
    const Event event{-1, 0};
    if (events) xQueueSend(events, &event, 0);
  });
  configTzTime(config.tz.c_str(), config.ntp.c_str());
  esp_sntp_set_sync_interval(3600000);
  current.state = State::Connected;
  current.disconnectReason = 0;
  snprintf(current.ip, sizeof current.ip, "%s", ip.c_str());
  snprintf(current.message, sizeof current.message, "polaczono");
  Serial.printf("network: connected SSID=%s IP=%s http://%s.local/ NTP interval=%lu ms\n",
                current.ssid, current.ip, current.hostname, (unsigned long)esp_sntp_get_sync_interval());
}
}
void begin(bool storageReady) {
  if (started) return;
  started = true;
  setenv("TZ", config.tz.c_str(), 1);
  tzset();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA); // Also needed for offline fingerprints.
  if (!storageReady) { problem(0, "pamiec dysku niedostepna"); return; }
  struct stat st{};
  if (stat("/ffat/wifi.txt", &st) != 0) {
    current.state = State::MissingConfig;
    Serial.println("network: brak wifi.txt, STA scans only");
    return;
  }
  std::string text;
  const char *why = "";
  if (!readNetworkFile("/ffat/wifi.txt", 4096, text, why)) { problem(0, why); return; }
  ConfigError err;
  if (!parseWifiConfig(text.c_str(), config, &err)) { problem(err.line, err.what); return; }
  setenv("TZ", config.tz.c_str(), 1);
  tzset();
  snprintf(current.ssid, sizeof current.ssid, "%s", config.ssid.c_str());
  snprintf(current.hostname, sizeof current.hostname, "%s", config.hostname.c_str());
  if (!loadPem(config.ca, config.caLine, false, caPem) ||
      !loadPem(config.cert, config.certLine, false, clientPem) ||
      !loadPem(config.key, config.keyLine, true, keyPem)) return;
  // Arduino's Enterprise overload rejects these lengths (unlike PSK).
  if (config.eap != Eap::None && (config.identity.size() > 64 || config.username.size() > 64 || config.password.size() > 64)) {
    problem(0, "EAP identity/username/password max 64 B"); return;
  }
  events = xQueueCreate(12, sizeof(Event));
  if (!events) { problem(0, "brak pamieci kolejki sieci"); return; }
  WiFi.onEvent(eventCallback);
  WiFi.setHostname(config.hostname.c_str());
  WiFi.setAutoReconnect(false); // Retry in loop, coordinated with fingerprint scans.
  configured = true;
  current.state = State::Connecting;
  snprintf(current.message, sizeof current.message, "laczenie");
  lastRetry = millis();
  if (config.eap == Eap::None) {
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
  } else {
    const wpa2_auth_method_t method = config.eap == Eap::Peap ? WPA2_AUTH_PEAP : config.eap == Eap::Ttls ? WPA2_AUTH_TTLS : WPA2_AUTH_TLS;
    const esp_eap_method_t allowed = config.eap == Eap::Peap ? ESP_EAP_TYPE_PEAP : config.eap == Eap::Ttls ? ESP_EAP_TYPE_TTLS : ESP_EAP_TYPE_TLS;
    if (esp_eap_client_set_eap_methods(allowed) != ESP_OK) { configured = false; problem(0, "nie mozna ustawic metody EAP"); return; }
    WiFi.begin(config.ssid.c_str(), method, config.identity.c_str(), optional(config.username), optional(config.password),
               optional(caPem), optional(clientPem), optional(keyPem));
    if (caPem.empty()) Serial.println("network: EAP bez CA - serwer nie jest sprawdzany");
  }
  Serial.printf("network: connecting SSID=%s\n", current.ssid);
}
void loop(uint32_t nowMs, bool scanning) {
  const bool ready = epochMs() > 1704067200000LL;
  if (ready != current.clockReady) {
    current.clockReady = ready;
    Serial.printf("network: clock %s\n", ready ? "NTP ready" : "not synchronized");
  }
  if (!configured) return;
  Event event;
  while (events && xQueueReceive(events, &event, 0) == pdTRUE) {
    if (event.type == -1) Serial.println("network: NTP synchronized");
    if (event.type == ARDUINO_EVENT_WIFI_STA_DISCONNECTED || event.type == ARDUINO_EVENT_WIFI_STA_LOST_IP)
      lostConnection(event.reason);
  }
  // Query the driver too: recover even when the callback queue overflowed.
  const bool connected = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
  if (connected) {
    const String ip = WiFi.localIP().toString();
    if (current.state != State::Connected || ip != current.ip) gotIp(ip);
  } else {
    if (current.state == State::Connected) lostConnection(0);
    if (!scanning && uint32_t(nowMs - lastRetry) >= 10000) {
      lastRetry = nowMs;
      WiFi.reconnect();
    }
  }
}
const Status &status() { return current; }
int64_t epochMs() { timeval tv{}; gettimeofday(&tv, nullptr); return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000; }
}
