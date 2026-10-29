#pragma once
#include <cctype>
#include <cstring>
#include <string>
#include <utility>

namespace net {
enum class Eap { None, Peap, Ttls, Tls };
struct ConfigError { int line = 0; const char *what = ""; };
struct WifiConfig {
  std::string ssid, password, identity, username, ca, cert, key;
  std::string ntp = "pool.ntp.org", tz = "CET-1CEST,M3.5.0,M10.5.0/3", hostname = "wibrometr";
  Eap eap = Eap::None;
  int caLine = 0, certLine = 0, keyLine = 0;
};
namespace config_detail {
inline std::string trim(const std::string &s) {
  const auto a = s.find_first_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}
inline bool relativePath(const std::string &s) {
  if (s.empty() || s.front() == '/' || s.find('\\') != std::string::npos || s.find(':') != std::string::npos) return false;
  size_t at = 0;
  do {
    auto end = s.find('/', at);
    auto part = s.substr(at, end == std::string::npos ? end : end - at);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    at = end + 1;
  } while (at <= s.size());
  return true;
}
inline bool hostnameValid(const std::string &s) {
  if (s.empty() || s.size() > 63 || s.front() == '-' || s.back() == '-') return false;
  for (unsigned char c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  return true;
}
}
inline bool parseWifiConfig(const char *text, WifiConfig &out, ConfigError *err = nullptr) {
  auto fail = [&](int line, const char *what) { if (err) *err = {line, what}; return false; };
  if (!text || std::strlen(text) > 4096) return fail(0, "wifi.txt za duzy (max 4096 B)");
  WifiConfig cfg;
  const char *names[] = {"ssid","password","eap","identity","username","ca","cert","key","ntp","tz","hostname"};
  int lines[11] = {};
  const char *p = text;
  if (std::strlen(p) >= 3 && !std::memcmp(p, "\xef\xbb\xbf", 3)) p += 3;
  int line = 0;
  while (*p) {
    ++line;
    const char *end = std::strchr(p, '\n');
    std::string row(p, end ? size_t(end - p) : std::strlen(p));
    p = end ? end + 1 : p + std::strlen(p);
    row = config_detail::trim(row.substr(0, row.find('#')));
    if (row.empty()) continue;
    auto eq = row.find('=');
    if (eq == std::string::npos) return fail(line, "brak znaku =");
    const auto name = config_detail::trim(row.substr(0, eq));
    const auto value = config_detail::trim(row.substr(eq + 1));
    int field = 0;
    while (field < 11 && name != names[field]) ++field;
    if (field == 11) return fail(line, "nieznany klucz");
    if (lines[field]) return fail(line, "powtorzony klucz");
    lines[field] = line;
    if (value.size() > (field == 0 ? 32u : 255u)) return fail(line, "wartosc za dluga");
    if (field >= 5 && field <= 7 && !config_detail::relativePath(value)) return fail(line, "sciezka musi byc wzgledna do dysku");
    switch (field) {
      case 0: cfg.ssid = value; break;
      case 1: cfg.password = value; break;
      case 2:
        if (value == "peap") cfg.eap = Eap::Peap;
        else if (value == "ttls") cfg.eap = Eap::Ttls;
        else if (value == "tls") cfg.eap = Eap::Tls;
        else return fail(line, "eap: wymagane peap, ttls lub tls");
        break;
      case 3: cfg.identity = value; break;
      case 4: cfg.username = value; break;
      case 5: cfg.ca = value; cfg.caLine = line; break;
      case 6: cfg.cert = value; cfg.certLine = line; break;
      case 7: cfg.key = value; cfg.keyLine = line; break;
      case 8: cfg.ntp = value; break;
      case 9: cfg.tz = value; break;
      case 10: cfg.hostname = value; break;
    }
  }
  if (cfg.cert.empty() != cfg.key.empty()) return fail(cfg.cert.empty() ? lines[7] : lines[6], "cert i key musza wystepowac razem");
  if (cfg.ssid.empty()) return fail(lines[0], "brak ssid");
  if (cfg.eap == Eap::None) {
    for (int i = 5; i <= 7; ++i) if (lines[i]) return fail(lines[i], "cert/key/ca wymaga eap");
    if (cfg.password.empty()) return fail(lines[1], "brak password");
  } else if (cfg.eap == Eap::Peap || cfg.eap == Eap::Ttls) {
    if (cfg.username.empty()) return fail(lines[4], "brak username");
    if (cfg.password.empty()) return fail(lines[1], "brak password");
    if (cfg.identity.empty()) cfg.identity = cfg.username;
  } else {
    if (cfg.identity.empty()) return fail(lines[3], "brak identity");
    if (cfg.cert.empty()) return fail(lines[6], "brak cert");
    if (cfg.key.empty()) return fail(lines[7], "brak key");
  }
  if (!config_detail::hostnameValid(cfg.hostname)) return fail(lines[10], "nieprawidlowy hostname");
  if (cfg.ntp.empty() || cfg.tz.empty()) return fail(cfg.ntp.empty() ? lines[8] : lines[9], "puste ntp/tz");
  out = std::move(cfg);
  if (err) *err = {};
  return true;
}
}
