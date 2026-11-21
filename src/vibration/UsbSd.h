#pragma once
#include <cstdint>
#include "MeasWriter.h"
#include "UsbOwnership.h"

// The TF card as a USB Mass Storage drive (raw sectors, SD.readRAW/writeRAW).
namespace usbsd {

// Call once in setup(), after MeasWriter::begin(). Waits up to ~2 s for the
// writer to mount the card. With a card: registers the MSC drive and starts USB;
// returns true. Without one: starts USB only (serial console), returns false; the
// user inserts a card and presses RESET.
bool begin(UsbOwnership &own, MeasWriter &writer);

// Main loop. At most every 100 ms: own.setEnumerated(host present).
void service(uint32_t nowMs);

} // namespace usbsd
