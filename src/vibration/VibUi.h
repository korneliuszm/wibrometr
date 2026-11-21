#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>
#include "MeasWriter.h"
#include "RecorderCore.h"
#include "SignalHistory.h"
#include "ThresholdUi.h"
#include "VibConfig.h"

// Everything the vibration recorder's single screen shows; plain data, filled by
// the main loop once per frame.
struct VibModel {
  RecorderCore::Mode mode = RecorderCore::Mode::Armed;
  MeasStatus sd;
  int thresholdDeci = thrui::kDefaultDeci;
  bool sensorLive = false;                              // A fresh sample within the last second.
  const SignalHistory<vib::kBins> *history = nullptr;   // Dynamic acceleration (gravity removed), m/s2.
  bool hostOwns = false;                                // The USB host has the card.
  uint32_t nowMs = 0;
  thrui::Button pressed = thrui::Button::None;          // Threshold button under the finger.
};

// Draws the whole screen (320x240) into the canvas. No hardware access, so it can
// be rendered on a PC.
void drawVibScreen(GFXcanvas16 &c, const VibModel &m);
