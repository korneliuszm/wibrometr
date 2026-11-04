#pragma once
#include "BatteryModel.h"

namespace hal {

// Cell voltage from the ADC (median of recent readings) and the charger line.
class BatteryMonitor {
public:
  void begin();
  void update(uint32_t nowMs); // Call often; samples every 100 ms, evaluates every second.
  const battery::Reading &reading() const { return reading_; }
  bool chargeKnown() const;
  bool charging() const { return charging_; }

private:
  static constexpr int kSamples = 16;
  float samples_[kSamples] = {};
  int count_ = 0, next_ = 0;
  uint32_t sampleAt_ = 0, evaluateAt_ = 0;
  int chargeHigh_ = 0;
  bool charging_ = false;
  battery::Reading reading_;
};

} // namespace hal
