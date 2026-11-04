#pragma once
#include <Adafruit_GFX.h>
#include "Displacement.h"

// Displacement screens: per-axis result of the last move with totals, the
// position over time of that move, and its path in the plane where it moved
// most. Drawing only, from the meter and a few flags.
struct DisplacementScreenState {
  const DisplacementMeter *meter = nullptr;
  bool live = false;           // Fresh samples arriving.
  const char *status = "";     // Shown when not live.
  float rateHz = 0;
  int screenPos = 0, screenCount = 1;
};

void drawDisplacementScreen(GFXcanvas16 &canvas, const DisplacementScreenState &state);
void drawDisplacementPlot(GFXcanvas16 &canvas, const DisplacementScreenState &state);
void drawDisplacementPath(GFXcanvas16 &canvas, const DisplacementScreenState &state);
