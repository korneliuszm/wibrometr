#pragma once
#include <Arduino.h>
#include <atomic>
#include "RecorderCore.h"
#include "RangeTrigger.h"

// VL53L0X range finder as the start trigger of the recorder. It ranges all the time
// (except while USB owns the card); a start requested while a measurement runs is ignored by
// RecorderCore, and the trigger needs the object to go away before it fires again
// (RangeTrigger). Reads go through hal::I2cBus; the library (Pololu) is used for the
// initialisation, mode changes and start/stop only, under hal::I2cBus::Exclusive.
class RangeTask {
public:
  // Initialises the sensor (blocks for a few hundred ms: call before the IMU task
  // starts) and starts the task. False when the sensor does not answer: then there is
  // no trigger at all, until the next power-up.
  bool begin(RecorderCore *core, int triggerMm, int mode);
  bool present() const { return present_; }
  // Last valid distance, RangeTrigger::kNoReading when none (not ranging, or no echo).
  uint16_t distanceMm() const { return distanceMm_.load(); }
  bool ranging() const { return ranging_.load(); }
  void countI2cError() {
    portENTER_CRITICAL(&mux_);
    ++stats_.i2cErrors;
    portEXIT_CRITICAL(&mux_);
  }
  // Serial 'd': every reading is printed with its status, signal rate and SPAD count.
  void toggleDebug() { debug_ = !debug_.load(); }
  // Readings since the last call (for the log, to see how the sensor behaves): valid range
  // min/max in mm, number of readings and how many of them had no valid range.
  struct Stats {
    uint16_t minMm = RangeTrigger::kNoReading, maxMm = 0, readings = 0, noEcho = 0, i2cErrors = 0;
  };
  Stats takeStats() {
    portENTER_CRITICAL(&mux_);
    const Stats s = stats_;
    stats_ = Stats{};
    portEXIT_CRITICAL(&mux_);
    return s;
  }
  // Any task: takes effect within ~10 ms.
  void setSettings(int triggerMm, int mode) {
    triggerMm_.store(triggerMm);
    mode_.store(mode);
  }

private:
  static void entry(void *self) { static_cast<RangeTask *>(self)->run(); }
  void run();

  RecorderCore *core_ = nullptr;
  bool present_ = false;
  std::atomic<int> triggerMm_{300}, mode_{1};
  std::atomic<uint16_t> distanceMm_{RangeTrigger::kNoReading};
  std::atomic<bool> ranging_{false}, debug_{false};
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Stats stats_;
};
