#pragma once
#include <Adafruit_GFX.h>
#include <cstdio>
#include "SignalHistory.h"
#include "UiTheme.h"

// Small drawing helpers shared by the page files.
namespace ui {

constexpr uint16_t kAxisColor[3] = {0xF800, 0x07E0, 0x3DFF}; // X red, Y green, Z blue.
constexpr char kAxisLetter[3] = {'X', 'Y', 'Z'};

inline float component(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

inline void text(GFXcanvas16 &c, int x, int y, int size, uint16_t color, const char *s) {
  c.setTextSize(size);
  c.setTextColor(color);
  c.setCursor(x, y);
  c.print(s);
}

// printf into the canvas at a position; returns the width in pixels.
template <typename... Args>
int textf(GFXcanvas16 &c, int x, int y, int size, uint16_t color, const char *fmt, Args... args) {
  char buffer[64];
  const int n = std::snprintf(buffer, sizeof buffer, fmt, args...);
  text(c, x, y, size, color, buffer);
  return (n < 0 ? 0 : n) * 6 * size;
}

// Smallest 1-2-5 step of at least `v`.
inline float niceAtLeast(float v) {
  float decade = 1;
  while (decade * 10 <= v) decade *= 10;
  while (decade > v) decade /= 10;
  for (float k : {1.0f, 2.0f, 5.0f, 10.0f})
    if (k * decade >= v) return k * decade;
  return 10 * decade;
}

} // namespace ui
