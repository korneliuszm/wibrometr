#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_pointer(a) (*(void *const *)(a))
using std::min; using std::max;
typedef bool boolean;
#include "Print.h"
class __FlashStringHelper;
inline void yield() {}
#include <string>
struct String : std::string { using std::string::string; String(const std::string &s) : std::string(s) {} };
