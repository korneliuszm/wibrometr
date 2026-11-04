#pragma once
#include "TouchGestures.h"

namespace hal {

// CST816D capacitive touch controller (single touch), polled over I2C.
class Cst816 {
public:
  // False if the chip does not answer; a different chip ID only warns.
  bool begin();
  // Current touch in screen coordinates (landscape 320x240); false on I2C error.
  bool read(TouchSample &out);
  uint8_t chipId() const { return chipId_; }

private:
  uint8_t chipId_ = 0;
};

} // namespace hal
