#pragma once
#include <Adafruit_GFX.h>
#include "SignalHistory.h"

// Gyro readout screen: angular rate about X, Y, Z in deg/s with centre-zero
// bars, and the angle turned about each axis since the last zeroing. Drawing
// only, from a snapshot, so it also renders off-device.
struct GyroScreenState {
  Accel rate;                  // Smoothed, bias removed, deg/s.
  Accel angle;                 // Integrated since zeroing, deg.
  bool live = false;           // False: show dashes and `status`.
  bool biasKnown = false;      // A rest has measured the gyro offset.
  float rateHz = 0;
  const char *status = "";
  int screenPos = 0, screenCount = 1;
};

void drawGyroScreen(GFXcanvas16 &canvas, const GyroScreenState &state);
