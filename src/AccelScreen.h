#pragma once
#include <Adafruit_GFX.h>
#include "SignalHistory.h"

// Acceleration readout screen: X, Y, Z in m/s2 and g with centre-zero bars,
// then the magnitude. Drawing only, from a snapshot, so it also renders
// off-device.
struct AccelScreenState {
  Accel value;                 // Already smoothed and tared, m/s2.
  bool live = false;           // False: show dashes and `status`.
  bool tared = false;
  float rateHz = 0;
  const char *status = "";     // Shown instead of the magnitude when not live.
  int screenPos = 0, screenCount = 1;
};

void drawAccelScreen(GFXcanvas16 &canvas, const AccelScreenState &state);
