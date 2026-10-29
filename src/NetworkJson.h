#pragma once
#include "ClientNetwork.h"
#include <string>
#include <cstdio>
namespace net {
inline std::string jsonText(const char *text) {
  std::string out = "\"";
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
    if (*p == '"' || *p == '\\') { out += '\\'; out += char(*p); }
    else if (*p == '\n') out += "\\n";
    else if (*p == '\r') out += "\\r";
    else if (*p == '\t') out += "\\t";
    else if (*p < 32) { char esc[7]; snprintf(esc, sizeof esc, "\\u%04x", unsigned(*p)); out += esc; }
    else out += char(*p);
  }
  return out + '"';
}
inline std::string statusJson(const Status &s) {
  return "{\"state\":" + jsonText(stateName(s.state)) + ",\"ssid\":" + jsonText(s.ssid) +
    ",\"ip\":" + jsonText(s.ip) + ",\"hostname\":" + jsonText(s.hostname) +
    ",\"clock_ready\":" + (s.clockReady ? "true" : "false") +
    ",\"error_line\":" + std::to_string(s.errorLine) + ",\"message\":" + jsonText(s.message) +
    ",\"disconnect_reason\":" + std::to_string(s.disconnectReason) + "}";
}
}
