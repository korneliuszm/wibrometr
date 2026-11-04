#pragma once
#include <cstdint>

// Display colours (RGB565) shared by all screens.
namespace ui {
constexpr uint16_t bg = 0x0841, grid = 0x2945, muted = 0x9CF3, edge = 0x4A69;
constexpr uint16_t button = 0x2965, buttonPressed = 0x5B0F, buttonOff = 0x1082;
constexpr uint16_t white = 0xFFFF, green = 0x07E0, yellow = 0xFFE0, red = 0xF800, blue = 0x3DFF;
// Axes as shown (screens, page, serial): Z with its sign turned. Navigation
// works on the sensor's own axes.
// The Feather build shows Z turned; a board whose axes are already as shown
// (the Waveshare) builds with -DUI_SHOWN_SIGN_Z=1.
#ifndef UI_SHOWN_SIGN_Z
#define UI_SHOWN_SIGN_Z -1
#endif
constexpr float shownSign[3] = {1, 1, UI_SHOWN_SIGN_Z};
inline float shown(float v, int axis) { return v * shownSign[axis]; }
} // namespace ui
