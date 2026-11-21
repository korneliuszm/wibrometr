#pragma once
#include <cmath>
#include <cstdint>

#include "SignalHistory.h"

// Threshold trigger on the high-passed acceleration magnitude. Allocation-free.
class TriggerDetector {
public:
  struct Config {
    float sampleRateHz = 500;
    float highPassTauS = 1.0f;
    uint32_t warmupUs = 1000000;
    uint32_t quietUs = 3000000;
  };
  enum class Event : uint8_t { None, Start, Stop };

  TriggerDetector() : TriggerDetector(Config{}) {}
  explicit TriggerDetector(Config cfg) : cfg_(cfg) {
    const float dt = 1.0f / cfg_.sampleRateHz;
    alpha_ = dt / (cfg_.highPassTauS + dt);
  }

  void reset() {
    haveFirst_ = false;
    warmedUp_ = false;
    recording_ = false;
    belowSince_ = false;
    dyn_ = {};
  }

  Event update(uint32_t tUs, const Accel &a, float thresholdMs2) {
    if (!haveFirst_) {
      haveFirst_ = true;
      firstUs_ = tUs;
      lp_ = a;
    } else {
      lp_.x += alpha_ * (a.x - lp_.x);
      lp_.y += alpha_ * (a.y - lp_.y);
      lp_.z += alpha_ * (a.z - lp_.z);
    }
    dyn_ = {a.x - lp_.x, a.y - lp_.y, a.z - lp_.z};
    // Latched so the uint32 clock wrapping (~71.6 min) cannot re-block Start.
    if (!warmedUp_ && static_cast<uint32_t>(tUs - firstUs_) >= cfg_.warmupUs) warmedUp_ = true;
    const bool above = deviation() >= thresholdMs2;
    if (recording_) {
      if (above) {
        belowSince_ = false;
      } else if (!belowSince_) {
        belowSince_ = true;
        belowStartUs_ = tUs;
      } else if (static_cast<uint32_t>(tUs - belowStartUs_) >= cfg_.quietUs) {
        recording_ = false;
        belowSince_ = false;
        return Event::Stop;
      }
      return Event::None;
    }
    if (above && warmedUp_) {
      recording_ = true;
      belowSince_ = false;
      return Event::Start;
    }
    return Event::None;
  }

  Accel dynamic() const { return dyn_; }
  float deviation() const { return std::sqrt(dyn_.x * dyn_.x + dyn_.y * dyn_.y + dyn_.z * dyn_.z); }
  bool recording() const { return recording_; }
  void cancelRecording() {
    recording_ = false;
    belowSince_ = false;
  }

private:
  Config cfg_;
  float alpha_ = 0;
  Accel lp_{}, dyn_{};
  bool haveFirst_ = false, warmedUp_ = false, recording_ = false, belowSince_ = false;
  uint32_t firstUs_ = 0, belowStartUs_ = 0;
};
