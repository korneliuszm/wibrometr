#pragma once
#include <cstdio>
#include <string>
namespace net {
// POSIX I/O shared by the firmware and host tests. Caller supplies a trusted
// base path; WifiConfig validates all user-provided relative paths first.
inline bool readNetworkFile(const char *path, size_t limit, std::string &out, const char *&why) {
  FILE *f = fopen(path, "rb");
  if (!f) { why = "brak pliku / blad otwarcia"; return false; }
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); why = "blad odczytu"; return false; }
  const long size = ftell(f);
  if (size <= 0 || size_t(size) > limit) { fclose(f); why = "pusty plik lub przekroczony limit"; return false; }
  rewind(f);
  std::string data(size_t(size), '\0');
  const size_t n = fread(&data[0], 1, data.size(), f);
  const bool ok = n == data.size() && !ferror(f);
  fclose(f);
  if (!ok || data.find('\0') != std::string::npos) { why = "niepelny odczyt lub znak NUL"; return false; }
  out = std::move(data); why = ""; return true;
}
inline bool pemEnvelopeValid(const std::string &pem, bool key) {
  if (pem.find("ENCRYPTED") != std::string::npos) return false;
  const char *types[] = {"CERTIFICATE", "PRIVATE KEY", "RSA PRIVATE KEY", "EC PRIVATE KEY"};
  const int first = key ? 1 : 0, last = key ? 4 : 1;
  for (int i = first; i < last; ++i) {
    const auto begin = pem.find(std::string("-----BEGIN ") + types[i] + "-----");
    if (begin != std::string::npos && pem.find(std::string("-----END ") + types[i] + "-----", begin) != std::string::npos) return true;
  }
  return false;
}
}
