#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>
#include "MeasStatus.h"
#include "RecorderCore.h"
#include "SignalHistory.h"
#include "ThresholdUi.h"
#include "VibConfig.h"
#include "BatteryModel.h"

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
  battery::Reading battery;                             // Cell voltage and estimate.
  bool charging = false;
  thrui::Button pressed = thrui::Button::None;          // Threshold button under the finger.
  float level = 0;                                      // Current |dynamic acceleration|, m/s2 (peak-held).
  uint32_t missed = 0;                                  // Sensor samples never read (since boot).
};

// Draws the whole screen (320x240) into the canvas. No hardware access, so it can
// be rendered on a PC.
void drawVibScreen(GFXcanvas16 &c, const VibModel &m);
