#pragma once
#include <cstdint>
#include "BatteryModel.h"
#include "ClientNetwork.h"
#include "Displacement.h"
#include "LineTracker.h"
#include "LogStatus.h"
#include "SignalHistory.h"
#include "Spectrum.h"
#include "StrokeCounter.h"
#include "TouchGestures.h"

// Everything the screens show, gathered in one snapshot, so drawing needs no
// hardware and can be rendered on a PC.
namespace ui {

constexpr int kBins = 136;                  // Waveform columns, 2 px each.
constexpr std::size_t kFftSize = 1024;      // ~2 s of samples at 500 Hz, ~0.5 Hz per bin.
constexpr std::size_t kSpectrumBins = kFftSize / 2 + 1;
constexpr uint32_t kBinMs = 40;             // Waveform bin width: window = kBins * kBinMs = 5.4 s.
constexpr float kWaveFullScale = 20.0f;     // Waveform vertical range, +-m/s2.
constexpr float kFftRange = 5.0f;           // Fixed spectrum full scale, m/s2.

// Line product pages first (as on the Feather), then the vibration analysis.
enum class Page : uint8_t { Line, Accel, Gyro, Disp, DispPlot, DispPath, Wave, FftX, FftY, FftZ, Strokes, Web,
                            Battery, Log, Count };
constexpr int kPageCount = int(Page::Count);

struct Model {
  uint32_t nowMs = 0;
  int pageIndex = 0;
  Page page = Page::Line;

  // Accelerometer.
  bool live = false;
  float rateHz = 0;
  const char *sensorStatus = "";            // Shown when there is no data.
  const char *sensorError = "";
  const char *i2cScan = "";
  Accel shown{};                            // Smoothed and tared, m/s2.
  bool tared = false;

  // Roller line, displacement, gyro, network (see LineApp).
  const line::Tracker *tracker = nullptr;
  bool configOk = false;
  int configLine = 0;
  const char *configWhat = "";
  bool scanBusy = false;
  const DisplacementMeter *meter = nullptr;
  Accel gyroRate{}, gyroAngle{};            // deg/s without the rest offset, deg since zeroing.
  bool gyroBiasKnown = false;
  const net::Status *network = nullptr;
  int webClients = 0;

  // Analysis (waveform, FFT, stroke counter).
  const SignalHistory<kBins> *history = nullptr;
  const SampleWindow<kFftSize> *window = nullptr;
  float *spectrum = nullptr;                // Scratch of kSpectrumBins floats.
  const StrokeCounter *strokes = nullptr;
  bool paused = false, zeroed = false;
  uint32_t droppedSamples = 0;

  // Power.
  battery::Reading battery;
  bool chargeKnown = true;

  // Logging.
  LogStatus log;
  char tag[9] = "";                         // Tag used for the next recording.
  bool keypadOpen = false;
  char keypadDigits[9] = "";

  // Touch feedback and messages.
  NavButton pressed = NavButton::None;
  const char *notice = nullptr;
};

} // namespace ui
