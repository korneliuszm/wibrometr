#pragma once
#include <Arduino.h>
#include <atomic>
#include "RecorderCore.h"
#include "SignalHistory.h"

// Sensor task of the vibration recorder: polls the QMI8658 at 500 Hz, feeds every
// sample to the RecorderCore (which writes measurements to the MeasSink) and sends
// the high-passed acceleration to the UI. Reconnects after I2C errors. Connection
// logic is a deliberate copy of src/waveshare/ImuTask.cpp (that one is tied to the
// SD logger and the gyroscope).
struct VibEvent {
  enum class Kind : uint8_t { Sample, Connecting, Waiting } kind = Kind::Sample;
  uint32_t ms = 0;    // millis() when queued.
  Accel dynamic{};    // RecorderCore::dynamic() after this sample, m/s2 (Sample only).
};

class VibImuTask {
public:
  // events: queue of VibEvent for the UI (lossy: dropped when full). core and sink
  // are used from the sensor task only (RecorderCore's thread-safe calls aside).
  void begin(QueueHandle_t events, RecorderCore *core, MeasSink *sink);
  float sampleRateHz() const { return sampleRateHz_.load(); }  // 0 until the sensor is found.
  float rangeG() const { return rangeG_.load(); }
  uint32_t dropped() const { return dropped_.load(); }         // UI events lost (queue full).

private:
  static void entry(void *self) { static_cast<VibImuTask *>(self)->run(); }
  void run();
  void emit(VibEvent::Kind kind, const Accel &dynamic = {});

  QueueHandle_t events_ = nullptr;
  RecorderCore *core_ = nullptr;
  MeasSink *sink_ = nullptr;
  std::atomic<uint32_t> dropped_{0};
  std::atomic<float> sampleRateHz_{0}, rangeG_{0};
};
