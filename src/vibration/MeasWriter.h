#pragma once
#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include "MeasFormat.h"
#include "RecorderCore.h"

// What the measurement writer reports to the UI; plain data.
struct MeasStatus {
  enum class State : uint8_t { NoCard, Idle, Recording, Error, Parked };
  State state = State::NoCard;
  char message[32] = "";      // Polish, no diacritics: "brak karty", "gotowa", ...
  unsigned number = 0;        // Current or last measurement (M<number>.CSV).
  uint32_t samples = 0;       // Samples written to the current/last file.
  uint32_t fileBytes = 0;
  uint32_t recordingMs = 0;   // Time since the file was opened (Recording only).
  uint32_t cardMb = 0;        // Card size.
  uint32_t dropped = 0;       // Records lost since boot because the queue was full.
  uint32_t measurements = 0;  // Mnnnn.CSV files in /MEAS.
};

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

  // Any task; asynchronous.
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
    float x, y, z;
  };
  using State = MeasStatus::State;

  static void taskEntry(void *self) { static_cast<MeasWriter *>(self)->run(); }
  void run();
  void process(const Item &it);
  bool mount();
  void startFile(int thresholdDeci);
  void finishFile(uint32_t dropped);
  bool flushBuffer();
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
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  MeasStatus status_;
  uint32_t startedMs_ = 0;               // Guarded by mux_.

  // Writer task only.
  State state_ = State::NoCard;
  File file_;
  bool full_ = false;                    // Numbering exhausted (M9999 exists).
  unsigned highest_ = 0, number_ = 0;
  uint32_t samples_ = 0, fileBytes_ = 0, measurements_ = 0;
  uint32_t nextMountMs_ = 0, lastWriteMs_ = 0, lastFlushMs_ = 0;
  char buffer_[4096 + 128];
  size_t used_ = 0;
};
