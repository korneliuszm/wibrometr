#pragma once
#include <cstdint>

// Display colours (RGB565) shared by all screens.
namespace ui {
constexpr uint16_t bg = 0x0841, grid = 0x2945, muted = 0x9CF3, edge = 0x4A69;
constexpr uint16_t white = 0xFFFF, green = 0x07E0, yellow = 0xFFE0, red = 0xF800;
} // namespace ui
