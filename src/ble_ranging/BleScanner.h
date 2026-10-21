#pragma once
#include "BleRanging.h"

// Continuous passive BLE scanning on the NimBLE host built into the
// framework, used through ESP-IDF directly (no Arduino BLE library). Adverts
// arrive in the NimBLE host task and are handed to the main loop through a
// FreeRTOS queue; pop() never blocks.
namespace ble {

class BleScanner {
public:
  bool begin();             // Starts the controller, host and scanning; false on error.
  bool pop(Advert &out);    // Next received advert, if any.
  uint32_t dropped() const; // Adverts lost because the queue was full.
  bool scanning() const;
};

} // namespace ble
