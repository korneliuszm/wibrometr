#pragma once
#include "SignalHistory.h"

namespace hal {

// QMI8658 6-axis IMU, accelerometer and gyroscope at one rate, polled over I2C.
class Qmi8658 {
public:
  struct Config {
    uint8_t range = 2;       // 0..3 = +-2/4/8/16 g.
    float odrHz = 500;       // 8000, 4000, 2000, 1000, 500, 250, 125, 62.5, 31.25.
    bool lowPass = true;     // Hardware low-pass before decimation.
    uint8_t lowPassMode = 3; // 0..3 = 2.66 / 3.63 / 5.39 / 13.37 % of the ODR.
    bool gyro = true;        // Gyroscope on, same ODR as the accelerometer.
    uint8_t gyroRange = 5;   // 0..7 = 16 .. 2048 dps; 5 = +-512 dps.
  };
  enum class Poll { NoData, Sample, Error };

  bool begin(const Config &config);
  // One new sample if the sensor has one; cheap, call at least twice per period.
  Poll poll(Accel &out, Accel &gyro); // Gyro in rad/s (zero when off).
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
