#include "hal/Cst816.h"
#include <Arduino.h>
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"

namespace hal {

namespace {
constexpr uint8_t kChipIdReg = 0xA7, kExpectedChipId = 0xB6;
constexpr uint8_t kGestureReg = 0x01;  // Gesture id, then touch count, XH, XL, YH, YL: six in a row.
} // namespace

bool Cst816::begin() {
  if (!I2cBus::probe(board::kTouchAddress)) return false;
  I2cBus::read(board::kTouchAddress, kChipIdReg, &chipId_, 1);
  if (chipId_ != kExpectedChipId)
    Serial.printf("Touch: chip ID 0x%02X (vendor demo expects 0x%02X), continuing\n", chipId_, kExpectedChipId);
  return true;
}

bool Cst816::read(TouchSample &out) {
  uint8_t buf[6];
  if (!I2cBus::read(board::kTouchAddress, kGestureReg, buf, sizeof buf)) return false;
  const uint8_t *raw = buf + 1;  // Touch count and coordinates, as before.
  if ((raw[0] & 0x0F) == 0) {
    out = TouchSample{};
    out.chipGesture = buf[0];  // The gesture is often reported with the release.
    return true;
  }
  const uint16_t rawX = uint16_t((raw[1] & 0x0F) << 8) | raw[2];
  const uint16_t rawY = uint16_t((raw[3] & 0x0F) << 8) | raw[4];
  out.chipGesture = buf[0];
  const ScreenPoint p = mapTouchLandscape(rawX, rawY, board::kScreenH);
  out.down = true;
  out.rawX = rawX;
  out.rawY = rawY;
  out.x = int16_t(constrain(p.x, 0, board::kScreenW - 1));
  out.y = int16_t(constrain(p.y, 0, board::kScreenH - 1));
  return true;
}

} // namespace hal
