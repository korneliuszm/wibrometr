#pragma once
#include "SignalHistory.h"

namespace hal {

// QMI8658 6-axis IMU, accelerometer only (gyroscope off), polled over I2C.
class Qmi8658 {
public:
  struct Config {
    uint8_t range = 2;       // 0..3 = +-2/4/8/16 g.
    float odrHz = 500;       // 8000, 4000, 2000, 1000, 500, 250, 125, 62.5, 31.25.
    bool lowPass = true;     // Hardware low-pass before decimation.
    uint8_t lowPassMode = 3; // 0..3 = 2.66 / 3.63 / 5.39 / 13.37 % of the ODR.
  };
  enum class Poll { NoData, Sample, Error };

  bool begin(const Config &config);
  // One new sample if the sensor has one; cheap, call at least twice per period.
  Poll poll(Accel &out);
  const char *error() const { return error_; }
  uint8_t whoAmI() const { return whoAmI_; }
  float sampleRateHz() const;
  float rangeG() const;

private:
  Config config_;
  uint8_t whoAmI_ = 0;
  char error_[40] = "";
};

} // namespace hal
