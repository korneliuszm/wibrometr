#pragma once
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "SampleRing.h"
#include "SignalHistory.h"
#include "TriggerDetector.h"

// Destination of one measurement. open() returns false when a measurement cannot
// start; none of the calls may block. triggerMm is the distance that started it.
struct MeasSink {
  virtual bool open(int triggerMm) = 0;
  virtual void write(int32_t relUs, const Accel &a) = 0;
  virtual void close() = 0;
  // Sensor samples lost while recording (read too late); recorded in the file.
  virtual void missed(uint32_t /*count*/) {}
  virtual ~MeasSink() = default;
};

// Recording state machine, started from outside (the range finder, see
// RangeTrigger.h) and ended by time. onSample/service/onGap/dynamic run on the
// sensor thread; requestStart, startPending, the setters, requestUsb and mode may be
// called from any thread. Allocation-free.
class RecorderCore {
public:
  enum class Mode : uint8_t { Armed, Recording, Usb };

  static constexpr uint32_t kMaxPreRollUs = 1000000;  // The ring holds 1 s at up to 2 kHz.

  // holdoffUs: after a refused open() the next start is ignored for this long.
  explicit RecorderCore(TriggerDetector::Config cfg = {}, uint32_t holdoffUs = 1000000)
      : det_(cfg), holdoffUs_(holdoffUs) {}

  // Recording time after the trigger and the part of the measurement before it.
  void setRecordUs(uint32_t us) { recordUs_.store(us, std::memory_order_relaxed); }
  void setPreRollUs(uint32_t us) { preRollUs_.store(us < kMaxPreRollUs ? us : kMaxPreRollUs, std::memory_order_relaxed); }
  // Start request: taken by the next onSample() while Armed (and dropped otherwise).
  // startPending() stays true until then, and mode() is already Recording when it
  // turns false after a successful start.
  void requestStart(int triggerMm) {
    triggerMm_.store(triggerMm, std::memory_order_relaxed);
    startWanted_.store(true, std::memory_order_release);
  }
  bool startPending() const { return startWanted_.load(std::memory_order_acquire); }
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
      setMode(Mode::Armed);
    }
  }

  void onSample(uint32_t tUs, const Accel &a, MeasSink &sink) {
    service(sink);
    if (mode() == Mode::Usb) {
      startWanted_.store(false, std::memory_order_release);
      return;
    }
    ring_.push(tUs, a);
    det_.update(tUs, a, INFINITY);  // Only the high-passed value for the chart: never triggers.
    if (mode() == Mode::Recording) {
      sink.write(static_cast<int32_t>(tUs - triggerUs_), a);
      startWanted_.store(false, std::memory_order_release);  // A start request now is moot.
      if (static_cast<uint32_t>(tUs - triggerUs_) >= activeRecordUs_) {
        sink.close();
        setMode(Mode::Armed);
      }
      return;
    }
    if (!startWanted_.load(std::memory_order_acquire)) return;
    if (holdoff_ && static_cast<uint32_t>(tUs - holdoffFromUs_) < holdoffUs_) {
      startWanted_.store(false, std::memory_order_release);
      return;
    }
    holdoff_ = false;
    const int mm = triggerMm_.load(std::memory_order_relaxed);
    if (!sink.open(mm)) {
      holdoff_ = true;
      holdoffFromUs_ = tUs;
      startWanted_.store(false, std::memory_order_release);
      return;
    }
    triggerUs_ = tUs;
    activeRecordUs_ = recordUs_.load(std::memory_order_relaxed);  // A change now applies to the next one.
    setMode(Mode::Recording);
    startWanted_.store(false, std::memory_order_release);  // After the mode: no gap where both look idle.
    const int32_t from = -static_cast<int32_t>(preRollUs_.load(std::memory_order_relaxed));
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
    startWanted_.store(false, std::memory_order_release);
  }
  void setMode(Mode m) { mode_.store(m, std::memory_order_release); }

  TriggerDetector det_;
  SampleRing<2048> ring_;  // 1 s pre-roll at up to 2 kHz.
  uint32_t holdoffUs_;
  uint32_t triggerUs_ = 0, holdoffFromUs_ = 0, activeRecordUs_ = 0;
  bool holdoff_ = false;
  std::atomic<uint32_t> recordUs_{5000000}, preRollUs_{kMaxPreRollUs};
  std::atomic<int> triggerMm_{0};
  std::atomic<bool> startWanted_{false}, usbWanted_{false};
  std::atomic<Mode> mode_{Mode::Armed};
};
