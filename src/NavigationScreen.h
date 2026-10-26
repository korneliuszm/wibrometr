#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>
#include "indoor_positioning/IndoorPositioning.h"

// "Indoor Navigation" screen: square map with the calibration triangle and
// the estimated position, plus a text panel. Drawing only; it reads a
// snapshot of the state, so it can also be rendered off-device.
struct NavScreenState {
  ips::CalibrationProgress progress;
  ips::Position position;
  bool calibrated = false;
  float sideM = 0;
  float refX[3] = {}, refY[3] = {}; // Reference points A, B, C (metres).
  int screenPos = 0, screenCount = 1;
  bool buttonHeld = false;
  uint32_t heldMs = 0;
  const char *notice = nullptr;     // Active notice, if any.
};

// Changes whenever anything visible changes; the caller redraws only then.
uint32_t navSignature(const NavScreenState &s, uint32_t now);
void drawNavigationScreen(GFXcanvas16 &canvas, const NavScreenState &s, uint32_t now);
