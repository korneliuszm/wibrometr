#include "hal/Cst816.h"
#include <Arduino.h>
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"

namespace hal {

namespace {
constexpr uint8_t kChipIdReg = 0xA7, kExpectedChipId = 0xB6;
constexpr uint8_t kTouchNumReg = 0x02; // Then XH, XL, YH, YL: five registers in a row.
} // namespace

bool Cst816::begin() {
  if (!I2cBus::probe(board::kTouchAddress)) return false;
  I2cBus::read(board::kTouchAddress, kChipIdReg, &chipId_, 1);
  if (chipId_ != kExpectedChipId)
    Serial.printf("Touch: chip ID 0x%02X (vendor demo expects 0x%02X), continuing\n", chipId_, kExpectedChipId);
  return true;
}

bool Cst816::read(TouchSample &out) {
  uint8_t raw[5];
  if (!I2cBus::read(board::kTouchAddress, kTouchNumReg, raw, sizeof raw)) return false;
  if ((raw[0] & 0x0F) == 0) {
    out = TouchSample{};
    return true;
  }
  const uint16_t rawX = uint16_t((raw[1] & 0x0F) << 8) | raw[2];
  const uint16_t rawY = uint16_t((raw[3] & 0x0F) << 8) | raw[4];
  const ScreenPoint p = mapTouchLandscape(rawX, rawY, board::kScreenH);
  out.down = true;
  out.x = int16_t(constrain(p.x, 0, board::kScreenW - 1));
  out.y = int16_t(constrain(p.y, 0, board::kScreenH - 1));
  return true;
}

} // namespace hal
