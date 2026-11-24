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

// The computer sees the card only in disk mode: by default (cable connected or not)
// the recorder keeps the card and the port is a plain serial console. Main loop only.
// setDiskMode(true) needs a connected host; the mode ends by itself when the cable
// is pulled. Without a card there is no disk mode at all.
bool hostConnected();  // A USB host is present (and there is a card to offer).
bool diskMode();
void setDiskMode(bool on);

// Main loop. At most every 100 ms: own.setEnumerated(host present and disk mode).
void service(uint32_t nowMs);

// Main loop, when the host lets go of the card. Revokes USB access and returns
// true once no transfer is running; from then on every transfer is refused
// (busy) and the card may be remounted (MeasWriter::unpark). Non-blocking: call
// again on later loops while it returns false.
bool release();
// Main loop, when the host takes the card: lifts the revoke from release().
void grant();

} // namespace usbsd
