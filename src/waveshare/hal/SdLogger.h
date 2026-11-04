#pragma once
#include <Arduino.h>
#include <atomic>
#include "LogFormat.h"
#include "LogStatus.h"
#include "SignalHistory.h"

namespace hal {

// Records acceleration to CSV files on the TF card (see LogFormat.h). A
// background task does all card access, so a slow write never stalls sampling:
// the sensor task only drops records into a queue.
class SdLogger {
public:
  using Status = LogStatus;
  using State = LogStatus::State;

  void begin();
  // Asynchronous requests. tag: hanger number digits ("" if unknown).
  void start(const char *tag, float sampleRateHz, float rangeG);
  void stop();
  bool recording() const { return recording_.load(); }
  // Never blocks; ignored unless recording.
  void pushSample(uint32_t tUs, const Accel &a);
  void pushBattery(uint32_t tUs, float millivolts, bool charging);
  Status status() const;

private:
  struct Record { uint8_t kind; uint32_t tUs; float a, b, c; };
  struct Command { uint8_t kind; float sampleRateHz, rangeG; char tag[logfmt::kMaxTagLen + 1]; };

  static void taskEntry(void *self) { static_cast<SdLogger *>(self)->run(); }
  void run();
  bool mount();
  void handle(const Command &c);
  void beginSession(const Command &c);
  void endSession();
  void flushBuffer();
  void setState(State s, const char *message);
  void fail(const char *message);

  QueueHandle_t records_ = nullptr, commands_ = nullptr;
  std::atomic<bool> recording_{false};
  std::atomic<uint32_t> dropped_{0};
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_;
  // Task-side state.
  void *file_ = nullptr; // File*, kept opaque here.
  unsigned highestSession_ = 0;
  uint32_t startedMs_ = 0, nextMountMs_ = 0, lastFlushMs_ = 0, lastWriteMs_ = 0;
  char buffer_[4096 + 128];
  size_t used_ = 0;
};

} // namespace hal
