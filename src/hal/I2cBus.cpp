#include "hal/I2cBus.h"
#include <Arduino.h>
#include <Wire.h>
#include "hal/BoardPins.h"

namespace hal {

namespace {
SemaphoreHandle_t mutex = nullptr;

struct Lock {
  bool held;
  Lock() : held(mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {}
  ~Lock() { if (held) xSemaphoreGive(mutex); }
};
} // namespace

void I2cBus::begin() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cHz);
  Wire.setTimeOut(20);
}

void I2cBus::recover() {
  Lock lock;
  Wire.end();
  Wire.begin(board::kI2cSda, board::kI2cScl, board::kI2cHz);
  Wire.setTimeOut(20);
}

bool I2cBus::probe(uint8_t address) {
  Lock lock;
  if (!lock.held) return false;
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool I2cBus::read(uint8_t address, uint8_t reg, uint8_t *data, size_t length) {
  Lock lock;
  if (!lock.held) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false; // Repeated start, then read.
  if (Wire.requestFrom(address, uint8_t(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = uint8_t(Wire.read());
  return true;
}

bool I2cBus::write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length) {
  Lock lock;
  if (!lock.held) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  for (size_t i = 0; i < length; ++i) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

} // namespace hal
