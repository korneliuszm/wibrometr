#pragma once
#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include "MeasFormat.h"
#include "MeasStatus.h"
#include "RecorderCore.h"


// Writes measurements to /MEAS/Mnnnn.CSV on the TF card (format: MeasFormat.h).
// A background task (core 0, priority 1) does all card access, so a slow card
// never stalls the sensor task: open/write/close only queue small items and never
// block. Buffering follows src/waveshare/hal/SdLogger.cpp.
//
// USB hand-over: park() closes an open file (after the queued records are
// written) and then stops all card access, leaving the card mounted so the USB
// task can use SD.readRAW/writeRAW. unpark() remounts (SD.end + SD.begin drops
// the FatFs cache, the host may have changed the card) and rescans numbering.
class MeasWriter : public MeasSink {
public:
  // Header values for every file. Starts the writer task (first mount attempt).
  void begin(float sampleRateHz, float rangeG);

  // MeasSink, sensor task. open() is false when the card is not ready (no card,
  // error, numbering exhausted) or the writer is parked or parking.
  bool open(int thresholdDeci) override;
  void write(int32_t relUs, const Accel &a) override;
  void close() override;
  void missed(uint32_t count) override { fileMissed_ += count; }

  // Any task; asynchronous.
  // Sample rate measured by the sensor task; written to the header of new files.
  void setMeasuredRate(float hz) { measuredHz_ = hz; }
  void park();
  void unpark();
  // True once parked: the writer no longer touches the card (false again as soon
  // as unpark() is called).
  bool parked() const { return parkWanted_.load() && isParked_.load(); }
  MeasStatus status() const;

private:
  struct Item {
    uint8_t kind;
    int32_t value;  // Sample: time relative to the trigger (us); open: threshold; close: drops.
    uint32_t extra; // Close: sensor samples missed during the measurement.
    float x, y, z;
  };
  using State = MeasStatus::State;

  static void taskEntry(void *self) { static_cast<MeasWriter *>(self)->run(); }
  void run();
  void process(const Item &it);
  bool mount();
  void startFile(int thresholdDeci);
  void finishFile(uint32_t dropped, uint32_t missed = 0);
  bool flushBuffer();
  bool remountCard();
  bool reopenAfterGlitch();
  void doPark();
  void doUnpark();
  void fail(const char *message);
  void setState(State s, const char *message);
  void publishCounters();

  QueueHandle_t queue_ = nullptr;
  float sampleRateHz_ = 0, rangeG_ = 0;
  std::atomic<bool> accepting_{false};   // Card ready for a new measurement.
  std::atomic<bool> parkWanted_{false}, isParked_{false};
  std::atomic<uint32_t> dropped_{0};     // All lost records.
  std::atomic<uint32_t> fileDropped_{0}; // Lost since the last open() (footer).
  std::atomic<float> measuredHz_{0};
  std::atomic<uint32_t> fileMissed_{0};  // Sensor samples missed since the last open().
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  MeasStatus status_;
  uint32_t startedMs_ = 0;               // Guarded by mux_.

  // Writer task only.
  State state_ = State::NoCard;
  File file_;
  bool full_ = false;                    // Numbering exhausted (M9999 exists).
  unsigned highest_ = 0, number_ = 0;
  uint32_t samples_ = 0, fileBytes_ = 0, measurements_ = 0, recoveries_ = 0;
  uint32_t nextMountMs_ = 0, lastWriteMs_ = 0, lastFlushMs_ = 0;
  char buffer_[8192 + 128];
  size_t used_ = 0;
};
