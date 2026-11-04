#pragma once
#include <cstdint>
namespace net {
enum class State { MissingConfig, ConfigError, Connecting, Connected, Disconnected };
struct Status {
  State state = State::MissingConfig;
  int errorLine = 0, disconnectReason = 0;
  char message[160] = "brak wifi.txt";
  char ssid[33] = {}, ip[16] = {}, hostname[64] = "wibrometr";
  bool clockReady = false;
};
void begin(bool storageReady);
void loop(uint32_t nowMs, bool scanning);
const Status &status(); // Arduino loop task only.
inline const char *stateName(State state) {
  switch (state) {
    case State::MissingConfig: return "missing_config";
    case State::ConfigError: return "config_error";
    case State::Connecting: return "connecting";
    case State::Connected: return "connected";
    case State::Disconnected: return "disconnected";
  }
  return "unknown";
}
int64_t epochMs();
}
