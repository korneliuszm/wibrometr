#include "hal/Qmi8658.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "Qmi8658Decode.h"
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"

namespace hal {

namespace {
constexpr uint8_t kWhoAmI = 0x00, kCtrl1 = 0x02, kCtrl2 = 0x03, kCtrl3 = 0x04, kCtrl5 = 0x06,
                  kCtrl7 = 0x08, kStatus0 = 0x2E, kTimestamp = 0x30 /* then TEMP, AX..AZ */, kAxL = 0x35 /* AX..AZ, then GX..GZ */, kReset = 0x60;
constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kSoftReset = 0xB0;
constexpr uint8_t kCtrl1AutoIncrement = 0x40; // ADDR_AI: multi-byte reads walk the registers.
constexpr uint8_t kAccelEnable = 0x01, kGyroEnable = 0x02; // CTRL7.
constexpr uint8_t kAccelDataReady = 0x01;     // STATUS0 bit 0.
// FIFO (datasheet: FIFO_WTM_TH, FIFO_CTRL, FIFO_SMPL_CNT, FIFO_STATUS, FIFO_DATA; CTRL9 commands).
constexpr uint8_t kCtrl9 = 0x0A, kFifoWtm = 0x13, kFifoCtrl = 0x14, kFifoCount = 0x15, kFifoData = 0x17,
                  kStatusInt = 0x2D;
constexpr uint8_t kCmdAck = 0x00, kCmdResetFifo = 0x04, kCmdRequestFifo = 0x05;
constexpr uint8_t kCmdDone = 0x80;            // STATUSINT bit 7.
constexpr uint8_t kFifoStream = 0x02, kFifoSize128 = 0x03 << 2; // FIFO_CTRL bits 1:0 and 3:2.
constexpr uint8_t kFifoOverflow = 0x20;       // FIFO_STATUS bit 5.
} // namespace

bool Qmi8658::begin(const Config &config) {
  config_ = config;
  haveStamp_ = false;
  auto fail = [this](const char *text) {
    std::snprintf(error_, sizeof error_, "%s", text);
    return false;
  };
  const uint8_t address = board::kImuAddress;
  if (!I2cBus::probe(address)) return fail("brak odpowiedzi 0x6B");
  uint8_t id = 0;
  if (!I2cBus::read(address, kWhoAmI, &id, 1)) return fail("odczyt WHO_AM_I nieudany");
  whoAmI_ = id;
  if (id != kWhoAmIValue) {
    std::snprintf(error_, sizeof error_, "WHO_AM_I=0x%02X, oczekiwano 0x05", id);
    return false;
  }
  I2cBus::writeByte(address, kReset, kSoftReset);
  delay(30);
  // The chip answers again once the reset is done.
  for (int i = 0; i < 10 && !I2cBus::read(address, kWhoAmI, &id, 1); ++i) delay(10);
  bool ok = I2cBus::writeByte(address, kCtrl1, kCtrl1AutoIncrement);
  ok = ok && I2cBus::writeByte(address, kCtrl2, uint8_t((config.range & 3) << 4) | qmi8658::odrCode(config.odrHz));
  const uint8_t gyroCtrl3 = config.gyro ? uint8_t(((config.gyroRange & 7) << 4) | qmi8658::odrCode(config.odrHz)) : 0;
  ok = ok && I2cBus::writeByte(address, kCtrl3, gyroCtrl3);
  uint8_t ctrl5 = 0;
  if (config.lowPass) {
    ctrl5 = uint8_t(((config.lowPassMode & 3) << 1) | 1);         // Accelerometer.
    if (config.gyro) ctrl5 |= uint8_t(((config.lowPassMode & 3) << 5) | 0x10); // Gyroscope.
  }
  ok = ok && I2cBus::writeByte(address, kCtrl5, ctrl5);
  if (config.fifo) {
    if (config.gyro) return fail("FIFO tylko bez zyroskopu");
    fifoCtrl_ = kFifoStream | kFifoSize128;
    ok = ok && I2cBus::writeByte(address, kFifoWtm, 16) && I2cBus::writeByte(address, kFifoCtrl, fifoCtrl_);
  }
  const uint8_t enable = config.gyro ? uint8_t(kAccelEnable | kGyroEnable) : kAccelEnable;
  ok = ok && I2cBus::writeByte(address, kCtrl7, enable);
  if (!ok) return fail("zapis konfiguracji nieudany");
  uint8_t back[2] = {};
  const uint8_t expected = uint8_t((config.range & 3) << 4) | qmi8658::odrCode(config.odrHz);
  if (!I2cBus::read(address, kCtrl2, &back[0], 1) || !I2cBus::read(address, kCtrl7, &back[1], 1) ||
      back[0] != expected || (back[1] & 0x03) != enable) {
    std::snprintf(error_, sizeof error_, "CTRL2 0x%02X/0x%02X CTRL7 0x%02X", expected, back[0], back[1]);
    return false;
  }
  delay(30); // First sample after enabling.
  if (config.fifo) {
    uint8_t fifoCtrl = 0;
    if (!I2cBus::read(address, kFifoCtrl, &fifoCtrl, 1) || (fifoCtrl & 0x0F) != fifoCtrl_) {
      std::snprintf(error_, sizeof error_, "FIFO_CTRL 0x%02X/0x%02X", fifoCtrl_, fifoCtrl);
      return false;
    }
    if (!command(kCmdResetFifo)) return fail("reset FIFO nieudany");
  }
  error_[0] = '\0';
  return true;
}

// CTRL9 protocol: write the command, wait for CmdDone in STATUSINT, acknowledge
// with CTRL_CMD_ACK, wait for CmdDone to clear.
bool Qmi8658::command(uint8_t cmd) {
  const uint8_t address = board::kImuAddress;
  if (!I2cBus::writeByte(address, kCtrl9, cmd)) return false;
  uint8_t status = 0;
  int i = 0;
  for (; i < 200; ++i) {
    if (I2cBus::read(address, kStatusInt, &status, 1) && (status & kCmdDone)) break;
    delayMicroseconds(25);
  }
  if (i == 200) return false;
  if (!I2cBus::writeByte(address, kCtrl9, kCmdAck)) return false;
  for (i = 0; i < 200; ++i) {
    if (I2cBus::read(address, kStatusInt, &status, 1) && !(status & kCmdDone)) return true;
    delayMicroseconds(25);
  }
  return false;
}

int Qmi8658::readFifo(Accel *out, int max, uint32_t &readUs, bool &overflow) {
  const uint8_t address = board::kImuAddress;
  overflow = false;
  if (!config_.fifo) return -1;
  // Datasheet 8.8: count first (write mode), then CTRL_CMD_REQ_FIFO, data, leave read mode.
  // While in read mode the chip DROPS new samples (8.7), so the window is kept short.
  uint8_t count[2];  // FIFO_SMPL_CNT, FIFO_STATUS.
  if (!I2cBus::read(address, kFifoCount, count, 2)) return -1;
  readUs = micros();
  overflow = count[1] & kFifoOverflow;
  // The count is in 2-byte words; one accelerometer sample is 6 bytes.
  int samples = 2 * ((int(count[1] & 0x03) << 8) | count[0]) / 6;
  if (samples > kFifoSamples) samples = kFifoSamples;
  if (samples > max) samples = max;
  if (samples == 0) return 0;
  if (!command(kCmdRequestFifo)) return -1;  // Sets FIFO_RD_MODE.
  bool ok = I2cBus::read(address, kFifoData, fifoRaw_, size_t(samples) * 6);
  // Leave read mode (clear FIFO_RD_MODE) whatever happened.
  ok = I2cBus::writeByte(address, kFifoCtrl, fifoCtrl_) && ok;
  if (!ok) return -1;
  uint8_t part[6];
  for (int i = 0; i < samples; ++i) {
    std::memcpy(part, fifoRaw_ + 6 * i, 6);
    out[i] = qmi8658::decode(part, config_.range);
  }
  return samples;
}

Qmi8658::Poll Qmi8658::poll(Accel &out, Accel &gyro) {
  uint8_t status = 0;
  if (!I2cBus::read(board::kImuAddress, kStatus0, &status, 1)) return Poll::Error;
  if (!(status & kAccelDataReady)) return Poll::NoData;
  uint8_t raw[12];
  const size_t length = config_.gyro ? 12 : 6; // Gyro registers follow the accelerometer's.
  if (!I2cBus::read(board::kImuAddress, kAxL, raw, length)) return Poll::Error;
  uint8_t part[6];
  std::memcpy(part, raw, 6);
  out = qmi8658::decode(part, config_.range);
  gyro = {};
  if (config_.gyro) {
    std::memcpy(part, raw + 6, 6);
    gyro = qmi8658::decodeGyro(part, config_.gyroRange);
  }
  return Poll::Sample;
}

Qmi8658::Poll Qmi8658::pollCounted(Accel &out, uint32_t &skipped) {
  skipped = 0;
  uint8_t raw[11];  // TIMESTAMP L/M/H, TEMP L/H, AX..AZ.
  if (!I2cBus::read(board::kImuAddress, kTimestamp, raw, sizeof raw)) return Poll::Error;
  // Only the low byte counts: the three bytes are not latched together, and a read
  // across a carry (0x..FF -> 0x..00) mixed old and new bytes (a false jump of
  // 256 was seen). One byte is consistent and covers 255 samples (~0.5 s).
  const uint32_t stamp = raw[0];
  if (haveStamp_ && stamp == lastStamp_) return Poll::NoData;
  if (haveStamp_) skipped = ((stamp - lastStamp_) & 0xFF) - 1;
  lastStamp_ = stamp;
  haveStamp_ = true;
  uint8_t part[6];
  std::memcpy(part, raw + 5, 6);
  out = qmi8658::decode(part, config_.range);
  return Poll::Sample;
}

float Qmi8658::sampleRateHz() const { return qmi8658::odrHz(qmi8658::odrCode(config_.odrHz)); }
float Qmi8658::rangeG() const { return qmi8658::rangeG(config_.range); }

} // namespace hal
