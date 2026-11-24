#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>
#include "MeasStats.h"
#include "LiveSpectrum.h"
#include "MeasStatus.h"
#include "PeakLog.h"
#include "RecorderCore.h"
#include "SignalHistory.h"
#include "RangeTrigger.h"
#include "RangeUi.h"
#include "UsbPageUi.h"
#include "VibConfig.h"
#include "BatteryModel.h"

// Everything the vibration recorder's screens show; plain data, filled by
// the main loop once per frame.
using VibPeaks = PeakLog<vib::kMaxPeaks>;

struct VibModel {
  int page = 0;                                         // 0: live chart and statistics, 1: peaks of the session, 2: FFT, 3: range finder settings, 4: USB disk mode.
  const VibPeaks *peaks = nullptr;                      // Peak of every measurement since power-up.
  RecorderCore::Mode mode = RecorderCore::Mode::Armed;
  MeasStatus sd;
  enum class RangeState : uint8_t { Missing, Ranging, Paused };
  rangeui::Settings range;                              // Range finder settings in use.
  RangeState rangeState = RangeState::Ranging;          // Missing: no sensor; Paused: not ranging (recording, USB).
  uint16_t rangeMm = RangeTrigger::kNoReading;          // Last valid distance (kNoReading: none).
  bool sensorLive = false;                              // A fresh sample within the last second.
  const SignalHistory<vib::kBins> *history = nullptr;   // Dynamic acceleration (gravity removed), m/s2.
  bool hostOwns = false;                                // The USB host has the card.
  uint32_t nowMs = 0;
  battery::Reading battery;                             // Cell voltage and estimate.
  bool charging = false;
  int calibStep = -1;                                   // Touch calibration: target shown (-1: not calibrating).
  bool usbHost = false;                                 // A computer is connected over USB.
  bool usbDisk = false;                                 // Disk mode: the card is offered to the computer.
  usbui::Button usbButton = usbui::Button::None;        // The USB page button under the finger.
  bool cleanConfirm = false;                            // "Clear the card" was pressed once: press again to do it.
  int cleanResult = -1;                                 // Files removed by the last cleanup, -1 = none shown.
  rangeui::Press rangePress;                            // Settings button under the finger.
  float level = 0;                                      // Current |dynamic acceleration|, m/s2 (peak-held).
  const LiveSpectrum *live = nullptr;                   // Spectrum of the live signal (null or not valid: none yet).
  const MeasStats *last = nullptr;                      // Statistics of the last measurement (null: none yet).
  unsigned lastNumber = 0;                              // Its file number (M<number>.CSV).
  uint32_t missed = 0;                                  // Sensor samples never read (since boot).
};

// Draws the whole screen (the page in m.page) (320x240) into the canvas. No hardware access, so it can
// be rendered on a PC.
void drawVibScreen(GFXcanvas16 &c, const VibModel &m);
