#pragma once
#include <Arduino.h>
#include <atomic>
#include "SignalHistory.h"
#include "hal/SdLogger.h"

// Sensor task: polls the QMI8658, timestamps samples, and delivers them to the
// UI (queue) and the SD logger. It also reconnects after errors and keeps a
// diagnostic text for the screen.
struct ImuEvent {
  enum class Kind : uint8_t { Sample, Connecting, Waiting };
  Kind kind = Kind::Sample;
  uint32_t ms = 0;   // millis() when queued.
  uint32_t us = 0;   // Sample time, uniform (see SampleClock).
  Accel value{};
};

struct ImuDiagnostics {
  char error[40] = "";
  char i2cScan[40] = "-";
};

class ImuTask {
public:
  void begin(QueueHandle_t events, hal::SdLogger *logger);
  ImuDiagnostics diagnostics() const;
  uint32_t dropped() const { return dropped_.load(); }
  float sampleRateHz() const { return sampleRateHz_; }
  float rangeG() const { return rangeG_; }

private:
  static void entry(void *self) { static_cast<ImuTask *>(self)->run(); }
  void run();
  void emit(ImuEvent::Kind kind, uint32_t us = 0, const Accel &a = {});

  QueueHandle_t events_ = nullptr;
  hal::SdLogger *logger_ = nullptr;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  ImuDiagnostics diag_;
  std::atomic<uint32_t> dropped_{0};
  float sampleRateHz_ = 0, rangeG_ = 0;
};
