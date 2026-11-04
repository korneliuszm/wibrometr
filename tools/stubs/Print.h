#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>
class Print {
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t r = 0; while (n--) r += write(*b++); return r; }
  size_t print(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t printf(const char *fmt, ...) { char buf[256]; va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a); return print(buf); }
};
