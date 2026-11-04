#include "hal/Battery.h"
#include <Arduino.h>
#include "hal/BoardPins.h"

namespace hal {

bool BatteryMonitor::chargeKnown() const { return board::kHasChargeDetect; }

void BatteryMonitor::begin() {
  pinMode(board::kBatteryAdc, INPUT);
  analogSetPinAttenuation(board::kBatteryAdc, ADC_11db);
  if (board::kHasChargeDetect) pinMode(board::kChargeDetect, INPUT);
}

void BatteryMonitor::update(uint32_t nowMs) {
  if (int32_t(nowMs - sampleAt_) >= 0) {
    sampleAt_ = nowMs + 100;
    // analogReadMilliVolts applies the chip's factory calibration.
    samples_[next_] = float(analogReadMilliVolts(board::kBatteryAdc)) * battery::kDividerRatio;
    next_ = (next_ + 1) % kSamples;
    if (count_ < kSamples) ++count_;
    if (board::kHasChargeDetect) {
      // The line is a plain digital level; three equal readings in a row switch the state.
      chargeHigh_ = digitalRead(board::kChargeDetect) ? min(chargeHigh_ + 1, 3) : max(chargeHigh_ - 1, -3);
      if (chargeHigh_ >= 3) charging_ = true;
      else if (chargeHigh_ <= -3) charging_ = false;
    }
  }
  if (count_ > 0 && int32_t(nowMs - evaluateAt_) >= 0) {
    evaluateAt_ = nowMs + 1000;
    float copy[kSamples];
    for (int i = 0; i < count_; ++i) copy[i] = samples_[i];
    reading_ = battery::evaluate(battery::medianMillivolts(copy, size_t(count_)), charging_, chargeKnown());
  }
}

} // namespace hal
