#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "SampleRing.h"
#include "SignalHistory.h"
#include "TriggerDetector.h"

// Destination of one measurement. open() returns false when a measurement cannot
// start; none of the calls may block.
struct MeasSink {
  virtual bool open(int thresholdDeci) = 0;
  virtual void write(int32_t relUs, const Accel &a) = 0;
  virtual void close() = 0;
  virtual ~MeasSink() = default;
};

// Recording state machine. onSample/service/onGap/dynamic run on the sensor thread;
// setThresholdDeci, thresholdDeci, requestUsb and mode may be called from any thread.
// Allocation-free.
class RecorderCore {
public:
  enum class Mode : uint8_t { Armed, Recording, Usb };

  explicit RecorderCore(TriggerDetector::Config cfg = {}, uint32_t preRollUs = 1000000,
                        uint32_t holdoffUs = 1000000)
      : det_(cfg), preRollUs_(preRollUs), holdoffUs_(holdoffUs) {}

  void setThresholdDeci(int deci) { thresholdDeci_.store(deci, std::memory_order_relaxed); }
  int thresholdDeci() const { return thresholdDeci_.load(std::memory_order_relaxed); }
  // Level request: applied by the next service() on the sensor thread.
  void requestUsb(bool active) { usbWanted_.store(active, std::memory_order_release); }
  Mode mode() const { return mode_.load(std::memory_order_acquire); }
  // Sensor thread only.
  Accel dynamic() const { return det_.dynamic(); }

  void service(MeasSink &sink) {
    const bool want = usbWanted_.load(std::memory_order_acquire);
    const Mode m = mode();
    if (want && m != Mode::Usb) {
      restart(sink);
      setMode(Mode::Usb);
    } else if (!want && m == Mode::Usb) {
      restart(sink);
      setMode(Mode::Armed);  // detector reset -> warm-up again
    }
  }

  void onSample(uint32_t tUs, const Accel &a, MeasSink &sink) {
    service(sink);
    if (mode() == Mode::Usb) return;
    ring_.push(tUs, a);
    const int deci = thresholdDeci();
    const auto ev = det_.update(tUs, a, deci / 10.0f);
    if (mode() == Mode::Recording) {
      sink.write(static_cast<int32_t>(tUs - triggerUs_), a);
      if (ev == TriggerDetector::Event::Stop) {
        sink.close();
        setMode(Mode::Armed);
      }
      return;
    }
    if (ev != TriggerDetector::Event::Start) return;
    if (holdoff_ && static_cast<uint32_t>(tUs - holdoffFromUs_) < holdoffUs_) {
      det_.cancelRecording();  // ignore Start during holdoff; detector keeps filtering
      return;
    }
    holdoff_ = false;
    if (!sink.open(deci)) {
      det_.cancelRecording();
      holdoff_ = true;
      holdoffFromUs_ = tUs;
      return;
    }
    triggerUs_ = tUs;
    setMode(Mode::Recording);
    const int32_t from = -static_cast<int32_t>(preRollUs_);
    for (std::size_t i = 0; i < ring_.size(); ++i) {  // oldest first; the trigger sample is last
      const auto e = ring_.at(i);
      const int32_t rel = static_cast<int32_t>(e.tUs - triggerUs_);
      if (rel >= from) sink.write(rel, e.a);
    }
  }

  // Sample stream interrupted: close a running measurement and start over.
  void onGap(MeasSink &sink) {
    if (mode() == Mode::Usb) return;
    restart(sink);
    setMode(Mode::Armed);
  }

private:
  void restart(MeasSink &sink) {
    if (mode() == Mode::Recording) sink.close();
    ring_.clear();
    det_.reset();
    holdoff_ = false;
  }
  void setMode(Mode m) { mode_.store(m, std::memory_order_release); }

  TriggerDetector det_;
  SampleRing<512> ring_;
  uint32_t preRollUs_, holdoffUs_;
  uint32_t triggerUs_ = 0, holdoffFromUs_ = 0;
  bool holdoff_ = false;
  std::atomic<int> thresholdDeci_{20};
  std::atomic<bool> usbWanted_{false};
  std::atomic<Mode> mode_{Mode::Armed};
};
