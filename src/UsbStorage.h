// src/UsbStorage.h
#pragma once
#include "StationConfig.h"

namespace storage {

// Exposes the "ffat" partition over USB Mass Storage AND over `/ffat`
// POSIX files -- both are the one filesystem (see UsbStorage.cpp for why
// that matters): `config.csv` is read once by `readConfig()` (call at
// boot, before the host could plausibly have mounted the drive) and the
// host can drop a new one onto the USB drive between runs. `appendDistanceRow()`
// writes directly to `/ffat` and is a no-op while the host has the drive
// mounted (`isMounted()`): the device is stationary and being read when
// that is true, it never writes `distances_last_run.csv` mid-run because
// USB is physically disconnected during a run.
// ponytail: no RAM queue for writes attempted while mounted — add one if
// a write-while-mounted case ever shows up on real hardware.
bool begin();
bool readConfig(line::StationConfig &out);
void appendDistanceRow(const char *csvRow);
bool isMounted();

} // namespace storage
