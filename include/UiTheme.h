#pragma once
#include <cstdint>

// Display colours (RGB565) shared by all screens.
namespace ui {
constexpr uint16_t bg = 0x0841, grid = 0x2945, muted = 0x9CF3, edge = 0x4A69;
constexpr uint16_t white = 0xFFFF, green = 0x07E0, yellow = 0xFFE0, red = 0xF800, blue = 0x3DFF;
// Axes as shown (screens, page, serial): Z with its sign turned. Navigation
// works on the sensor's own axes.
constexpr float shownSign[3] = {1, 1, -1};
inline float shown(float v, int axis) { return v * shownSign[axis]; }
} // namespace ui
