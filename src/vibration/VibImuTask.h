#pragma once
#include <Arduino.h>
#include <atomic>
#include "RecorderCore.h"
#include "SignalHistory.h"

// Sensor task of the vibration recorder: reads the QMI8658 FIFO (2 kHz) every few ms, feeds every
// sample to the RecorderCore (which writes measurements to the MeasSink) and sends
// the high-passed acceleration to the UI. Reconnects after I2C errors. Connection
// logic is a deliberate copy of src/waveshare/ImuTask.cpp (that one is tied to the
// SD logger and the gyroscope).
struct VibEvent {
  enum class Kind : uint8_t { Sample, Connecting, Waiting } kind = Kind::Sample;
  uint32_t ms = 0;    // Sample time in ms (same base as millis()).
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
  uint32_t missed() const { return missed_.load(); }           // Sensor samples lost (read too late / FIFO overflow).
  uint32_t overflows() const { return overflows_.load(); }     // Reads that found the FIFO overflowed.
  float measuredHz() const { return measuredHz_.load(); }      // Real sample rate (0 until measured, ~10 s).

private:
  static void entry(void *self) { static_cast<VibImuTask *>(self)->run(); }
  void run();
  void emit(VibEvent::Kind kind, const Accel &dynamic = {}, uint32_t ms = 0);

  QueueHandle_t events_ = nullptr;
  RecorderCore *core_ = nullptr;
  MeasSink *sink_ = nullptr;
  std::atomic<uint32_t> dropped_{0}, missed_{0}, overflows_{0};
  std::atomic<float> sampleRateHz_{0}, rangeG_{0}, measuredHz_{0};
};
