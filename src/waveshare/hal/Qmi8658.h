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
    bool fifo = false;       // Accelerometer samples through the 128-sample FIFO (stream mode).
  };
  static constexpr int kFifoSamples = 128;
  enum class Poll { NoData, Sample, Error };

  bool begin(const Config &config);
  // One new sample if the sensor has one; cheap, call at least twice per period.
  Poll poll(Accel &out, Accel &gyro); // Gyro in rad/s (zero when off).
  // Accelerometer only: one burst of the sample counter (TIMESTAMP low byte, +1 per sample),
  // temperature and XYZ. Sample when the counter moved; skipped = samples made
  // since the previous one that were never read (exact, from the chip's counter).
  Poll pollCounted(Accel &out, uint32_t &skipped);
  uint32_t lastStamp() const { return lastStamp_; }

  // FIFO mode (Config::fifo, accelerometer only): moves up to `max` samples, oldest
  // first, into out. NOTE (datasheet 8.7): the chip drops every sample produced
  // while the FIFO is in read mode, and over 400 kHz I2C a read takes ~135 us per
  // sample plus the CTRL9 handshake, so FIFO reading always loses part of the data
  // (measured: ~35 % at 1 kHz). Not used by the recorder; kept for reference. readUs: micros() when the count was read (the newest sample is
  // at most one period older). overflow: the FIFO was full (samples were lost).
  // Returns the number of samples, or -1 on an I2C error.
  int readFifo(Accel *out, int max, uint32_t &readUs, bool &overflow);
  const char *error() const { return error_; }
  uint8_t whoAmI() const { return whoAmI_; }
  float sampleRateHz() const;
  float rangeG() const;

private:
  bool command(uint8_t cmd);  // CTRL9 command with the CmdDone / ACK handshake.

  Config config_;
  uint8_t fifoCtrl_ = 0;
  uint32_t lastStamp_ = 0;
  bool haveStamp_ = false;
  uint8_t fifoRaw_[kFifoSamples * 6];
  uint8_t whoAmI_ = 0;
  char error_[40] = "";
};

} // namespace hal
