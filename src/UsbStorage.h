// src/UsbStorage.h
#pragma once
#include "StationConfig.h"

namespace storage {

// Exposes the "ffat" partition over USB Mass Storage AND over `/ffat`
// POSIX files -- both are the one filesystem (see UsbStorage.cpp for why
// that matters): `config.csv` is read once by `readConfig()` (call at
// boot, before the host could plausibly have mounted the drive) and the
// host can drop a new one onto the USB drive between runs. `appendLogRow()`
// writes directly to `/ffat` and is a no-op while the host has the drive
// mounted (`isMounted()`): the device is stationary and being read when
// that is true, it never writes a log file mid-run because
// USB is physically disconnected during a run.
// ponytail: no RAM queue for writes attempted while mounted — add one if
// a write-while-mounted case ever shows up on real hardware.
bool begin();
// Expose MSC only after startup configuration and PEM reads have finished.
void startUsb();
// false without a valid config.csv; `err` says why (line 0, "brak pliku"
// when there is none).
bool readConfig(line::StationConfig &out, line::ConfigError &err);
// Appends time-prefixed `csvRow` to logs/distances_last_run.csv and to "/ffat/logs/<file>.csv"
// (header first when new): one file per ride between two stations ("B2-B3")
// and per station's technological motions ("B3_tech"). A file whose header
// is not the current one (older columns) is moved aside to "<file>_old.csv"
// first, so no file mixes two layouts.
void appendLogRow(const char *file, const char *csvRow);
bool isMounted();

} // namespace storage
