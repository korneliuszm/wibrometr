# Konfiguracja stacji CSV + USB MSC + log odległości + klasyfikacja ruchu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Device reads `config.csv` from a USB Mass Storage partition at
boot and uses it as the authoritative source of station layout, replacing
auto-learned gaps/directions and Wi-Fi position correction; it classifies
technological motion separately from transport and logs measured
distances/angles for later manual correction of the CSV.

**Architecture:** Three new pure-C++ modules (`StationConfig`,
`MotionClassifier`, `DistanceLog`) following the existing style of
`include/*.h` (header-only, no dynamic allocation, tested off-device with
plain `g++`). One new integration module (`UsbStorage`) exposes an `FFat`
partition over `USBMSC` and wires `config.csv` into `line::Tracker` at
boot and `distances_last_run.csv` out of it after every stop. `Tracker`
gets one new flag (`configAuthoritative_`) guarding the three existing
auto-learn/auto-correct call sites.

**Tech Stack:** PlatformIO / Arduino-ESP32 (pioarduino 55.03.36), C++17,
`FFat` (Arduino ESP32 FAT filesystem), `USBMSC` (Arduino ESP32 USB Mass
Storage), existing `include/LineTracker.h`.

**Spec:** `docs/superpowers/specs/2026-10-28-skid-station-config-design.md`

## Global Constraints

- Board: Adafruit 5483 ESP32-S3 TFT Feather, **4 MB flash**. Firmware
  already uses 61% flash with Wi-Fi fingerprinting (per README). USB MSC +
  FFat must fit — Task 1 verifies this before any other task proceeds.
- `config.csv` is read **only at boot** (no live reload).
- When `config.csv` loads successfully: auto-learning of `learned.gap[]`
  and `learned.gapDir[]`, and Wi-Fi-driven station correction in
  `applyScan()`, are both disabled. Wi-Fi scanning and fingerprint display
  keep working (needed for the dashboard and distance log), only the
  `station_ = ...` writes from Wi-Fi are skipped.
- `distances_last_run.csv` is written directly to FFat after each stop
  (USB is physically disconnected during a run); writes are skipped while
  `USBMSC` reports the host has it mounted.
- No new third-party library dependencies — `FFat`/`USBMSC` ship with the
  pinned Arduino-ESP32 core already in `platformio.ini`.
- Tests follow the existing house style: plain `g++ -std=c++17 -Iinclude
  test/<name>.cpp -o <bin> && ./<bin>`, runtime `check(bool, const char*)`
  assertions (see `test/line_checks.cpp`), no test framework.

---

### Task 1: Flash-budget spike (USB MSC + FFat fit check)

**Files:**
- Create: `partitions.csv` (custom partition table)
- Modify: `platformio.ini` (add `board_build.partitions`, `board_build.filesystem`)
- Modify: `src/main.cpp` (temporary minimal `USBMSC` + `FFat.begin()` probe)

**Interfaces:**
- Produces: a working `partitions.csv` with an `ffat` partition, confirmed
  to compile within flash, that Task 4 builds on. No other task depends on
  the probe code in `main.cpp` (it is removed at the end of this task).

- [ ] **Step 1: Write the custom partition table**

Default Feather ESP32-S3 TFT partitioning leaves no room for a FAT
partition; add one. 4 MB flash, app partition needs the firmware (currently
~2.4 MB of 4 MB = 61%), leave 512 KB for `ffat` (enough for `config.csv`
and `distances_last_run.csv`, both tiny text files):

```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x300000,
ffat,     data, fat,     0x310000,0x80000,
```

Save as `partitions.csv` in the repo root.

- [ ] **Step 2: Point the build at the custom table and FFat**

In `platformio.ini`, inside `[env:feather]`, add:

```ini
board_build.partitions = partitions.csv
board_build.filesystem = fatfs
```

- [ ] **Step 3: Add a temporary USB MSC + FFat probe to `main.cpp`**

At the top of `setup()` in `src/main.cpp` (before the existing MPU6050 and
display init), add — temporarily, this is removed in Step 6:

```cpp
#include "FFat.h"
#include "USB.h"
#include "USBMSC.h"

static USBMSC msc;
static bool mscProbeOk = false;

static int32_t mscRead(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  return FFat.readRaw(lba, offset, buf, size); // placeholder call shape; see Step 4 note
}
static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  return size;
}
static bool mscStartStop(uint8_t power, bool start, bool eject) { return true; }

void probeUsbMsc() {
  mscProbeOk = FFat.begin(true, "/ffat", 2, "ffat"); // format-if-needed, 2 FAT dir entries cache
  Serial.printf("FFat.begin: %s, total=%u used=%u\n", mscProbeOk ? "ok" : "FAIL",
                (unsigned)FFat.totalBytes(), (unsigned)FFat.usedBytes());
}
```

Note: the exact `USBMSC` read/write callback signature varies slightly
across Arduino-ESP32 core versions pinned by `pioarduino 55.03.36` — Step 4
is to get this to compile, not to get it fully correct; fix signatures to
match whatever the installed `USBMSC.h` declares (check
`~/.platformio` / `${sysenv.TEMP}/vibrationSensor-pio-core` cache after
first `pio run` failure, the compiler error names the expected signature).

Call `probeUsbMsc();` once near the top of `setup()`.

- [ ] **Step 4: Build and read the flash report**

Run:
```powershell
pio run
```

Expected: build succeeds (after fixing any `USBMSC` callback signature
mismatch per Step 3's note) and the final output lines report flash usage,
e.g. `Flash: [=====     ]  NN.N% (used M bytes from N bytes)`.

- [ ] **Step 5: Decide go/no-go and record it**

- If flash usage after the probe is **≤ 90%**: proceed, this fits.
- If **> 90%**: the firmware does not fit with Wi-Fi fingerprinting +
  USB MSC together on this board. Stop here and flag it back to the user —
  do not silently drop Wi-Fi fingerprinting, that is a product decision,
  not an implementation one.

Append the measured percentage to
`docs/superpowers/specs/2026-10-28-skid-station-config-design.md` under a
new `## Flash budget (measured 2026-10-28)` heading, one line.

- [ ] **Step 6: Remove the probe, keep the partition table**

Delete the `#include`s, `msc`/`mscProbeOk` globals, `mscRead`/`mscWrite`/
`mscStartStop`/`probeUsbMsc` functions and the `probeUsbMsc();` call added
in Step 3. Keep `partitions.csv` and the two `platformio.ini` lines from
Step 2 — Task 4 reuses them for the real implementation.

- [ ] **Step 7: Commit**

```bash
git add partitions.csv platformio.ini docs/superpowers/specs/2026-10-28-skid-station-config-design.md
git commit -m "build: reserve FFat partition, confirm flash budget for USB MSC"
```

---

### Task 2: `StationConfig` — CSV parser

**Files:**
- Create: `include/StationConfig.h`
- Test: `test/station_config_checks.cpp`

**Interfaces:**
- Produces: `line::StationRow`, `line::StationConfig`,
  `bool line::parseStationConfig(const char *text, StationConfig &out)` —
  consumed by Task 3 (`Tracker::applyStationConfig`) and Task 4
  (`UsbStorage`, which reads the file into a buffer and calls this).

- [ ] **Step 1: Write the failing test**

```cpp
// test/station_config_checks.cpp
// g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc
#include <cstdio>
#include "StationConfig.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}

int main() {
  using namespace line;

  {
    StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed\n"
      "ST01,X+,10,false,true,none,,,\n"
      "ST02,X+,10,true,false,lift,Z,0.4,0.1\n"
      "ST03,Y+,8,false,true,rotation_z,Z,90,\n";
    check(parseStationConfig(text, cfg), "well-formed 3-row file parses");
    check(cfg.count == 3, "three rows read");
    check(cfg.row[1].passthrough == true, "passthrough true parsed");
    check(cfg.row[1].motion == Motion::Lift, "lift motion parsed");
    check(std::fabs(cfg.row[1].motionRange - 0.4f) < 1e-6f, "motion range parsed");
    check(cfg.row[2].direction == Direction::YPlus, "Y+ direction parsed");
    check(cfg.row[2].motion == Motion::RotationZ, "rotation_z motion parsed");
    check(std::fabs(cfg.row[2].motionRange - 90.0f) < 1e-6f, "rotation range in degrees parsed");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,none,,,\nST02,bogus,10,false,true,none,,,\n";
    check(!parseStationConfig(text, cfg), "unknown direction rejects the whole file");
  }
  {
    StationConfig cfg;
    const char *text = "ST01,X+,10,false,true,none,,,\nST02,X+,10,false\n"; // missing columns
    check(!parseStationConfig(text, cfg), "missing columns rejects the whole file");
  }
  {
    StationConfig cfg;
    check(!parseStationConfig("", cfg), "empty file is rejected (no fallback silently applied)");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc`
Expected: compile error, `StationConfig.h` does not exist yet.

- [ ] **Step 3: Write the implementation**

```cpp
// include/StationConfig.h
#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "LineTracker.h" // line::maxStations

namespace line {

enum class Direction : uint8_t { XPlus, XMinus, YPlus, YMinus };
enum class Motion : uint8_t { None, Lift, RotationZ, ZTransfer, Compound };

struct StationRow {
  char id[12] = {};
  Direction direction = Direction::XPlus;
  float distanceM = 0;
  bool passthrough = false;
  bool wifiScan = true;
  Motion motion = Motion::None;
  float motionRange = 0;  // metres (lift/z_transfer) or degrees (rotation_z); 0 = unset.
  float motionSpeed = 0;  // m/s or deg/s; 0 = unset (optional column).
};

struct StationConfig {
  static constexpr int maxRows = maxStations;
  int count = 0;
  StationRow row[maxRows];
};

namespace detail {
// Copies the next ','-separated field from `*p` into `out` (NUL-terminated,
// truncated to outSize - 1) and advances `*p` past the separator. Treats
// '\n' and end-of-string as the end of the row (does not advance past them).
inline void nextField(const char *&p, char *out, int outSize) {
  int n = 0;
  while (*p && *p != ',' && *p != '\n' && *p != '\r') {
    if (n < outSize - 1) out[n++] = *p;
    ++p;
  }
  out[n] = 0;
  if (*p == ',') ++p;
}
inline bool parseBool(const char *s, bool &out) {
  if (std::strcmp(s, "true") == 0) { out = true; return true; }
  if (std::strcmp(s, "false") == 0) { out = false; return true; }
  return false;
}
inline bool parseDirection(const char *s, Direction &out) {
  if (std::strcmp(s, "X+") == 0) { out = Direction::XPlus; return true; }
  if (std::strcmp(s, "X-") == 0) { out = Direction::XMinus; return true; }
  if (std::strcmp(s, "Y+") == 0) { out = Direction::YPlus; return true; }
  if (std::strcmp(s, "Y-") == 0) { out = Direction::YMinus; return true; }
  return false;
}
inline bool parseMotion(const char *s, Motion &out) {
  if (s[0] == 0 || std::strcmp(s, "none") == 0) { out = Motion::None; return true; }
  if (std::strcmp(s, "lift") == 0) { out = Motion::Lift; return true; }
  if (std::strcmp(s, "rotation_z") == 0) { out = Motion::RotationZ; return true; }
  if (std::strcmp(s, "z_transfer") == 0) { out = Motion::ZTransfer; return true; }
  if (std::strcmp(s, "compound") == 0) { out = Motion::Compound; return true; }
  return false;
}
// Parses one row. `line` must not contain the trailing '\n'. Returns false
// (leaving `out` untouched) if the row has the wrong shape or an unknown
// enum value. A row whose first field is "station_id" (the header) is
// skipped and reported as true with `out` untouched — caller checks that
// separately via `isHeader`.
inline bool isHeaderRow(const char *line) { return std::strncmp(line, "station_id,", 11) == 0; }

inline bool parseRow(const char *line, StationRow &out) {
  const char *p = line;
  char field[16];
  StationRow r;

  detail::nextField(p, field, sizeof(field));
  std::strncpy(r.id, field, sizeof(r.id) - 1);

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseDirection(field, r.direction)) return false;

  detail::nextField(p, field, sizeof(field));
  if (field[0] == 0) return false;
  r.distanceM = std::strtof(field, nullptr);

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseBool(field, r.passthrough)) return false;

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseBool(field, r.wifiScan)) return false;

  detail::nextField(p, field, sizeof(field));
  if (!detail::parseMotion(field, r.motion)) return false;

  detail::nextField(p, field, sizeof(field)); // motion_axis: accepted, not used (rotation_z is always Z).
  (void)field;

  detail::nextField(p, field, sizeof(field)); // motion_range
  r.motionRange = field[0] ? std::strtof(field, nullptr) : 0;

  detail::nextField(p, field, sizeof(field)); // motion_speed
  r.motionSpeed = field[0] ? std::strtof(field, nullptr) : 0;

  // All 9 columns must have been present: after the last field, only end of
  // row is left (no stray extra field starting here).
  if (*p != 0 && *p != '\n' && *p != '\r') return false;

  out = r;
  return true;
}
} // namespace detail

// Parses the whole file text (rows separated by '\n', CRLF tolerated, an
// optional header row is skipped). All-or-nothing: a single malformed or
// unrecognised row rejects the whole file (caller keeps the previous
// configuration, i.e. none, and falls back to auto-learning).
inline bool parseStationConfig(const char *text, StationConfig &out) {
  StationConfig result;
  const char *p = text;
  while (*p) {
    const char *lineStart = p;
    while (*p && *p != '\n') ++p;
    const char *lineEnd = p;
    if (*p == '\n') ++p;
    if (lineEnd == lineStart) continue; // blank line
    char buf[160];
    int n = int(lineEnd - lineStart);
    if (n >= (int)sizeof(buf)) return false; // row too long to be well-formed
    std::memcpy(buf, lineStart, n);
    buf[n] = 0;
    if (detail::isHeaderRow(buf)) continue;
    if (result.count >= StationConfig::maxRows) return false; // more rows than maxStations
    if (!detail::parseRow(buf, result.row[result.count])) return false;
    ++result.count;
  }
  if (result.count == 0) return false;
  out = result;
  return true;
}

} // namespace line
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc`
Expected: `all ok`, exit code 0.

- [ ] **Step 5: Commit**

```bash
git add include/StationConfig.h test/station_config_checks.cpp
git commit -m "feat: parse station config.csv (direction, distance, motion)"
```

---

### Task 3: Integrate `StationConfig` into `Tracker` (CSV authoritative)

**Files:**
- Modify: `include/LineTracker.h:126-136` (add fields to `Learned`/`Tracker` is not needed — config is applied, not persisted, see below)
- Modify: `include/LineTracker.h:138-209` (new `applyStationConfig`, new private flag)
- Modify: `include/LineTracker.h:293-355` (`applyScan`: guard the two `station_ = ...` correction writes)
- Modify: `include/LineTracker.h:796-811` (`finish()`: guard gap/direction auto-learn)
- Test: `test/line_checks.cpp` (extend with a CSV-authoritative scenario)

**Interfaces:**
- Consumes: `line::StationConfig`, `line::StationRow` from Task 2.
- Produces: `void Tracker::applyStationConfig(const StationConfig&)`,
  `bool Tracker::configAuthoritative() const` — consumed by Task 4
  (`UsbStorage`, calls `applyStationConfig` once at boot) and Task 5/6
  (`MotionClassifier`/`DistanceLog` integration, which need to know which
  station has a configured `Motion` and read it via a new
  `const StationRow *stationRow(int s) const` accessor added in this task).

- [ ] **Step 1: Write the failing test**

Add to `test/line_checks.cpp` (after the existing scenarios, same file —
it already builds one `Tracker` per scenario and feeds it synthetic
samples; this reuses that harness rather than adding a second file, since
it is testing `Tracker` itself, not a new module):

```cpp
// --- CSV-authoritative: config.csv overrides auto-learning ---
{
  line::Tracker t;
  line::StationConfig cfg;
  const char *text =
    "station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed\n"
    "ST01,X+,10,false,true,none,,,\n"
    "ST02,X+,5,false,true,none,,,\n"; // deliberately different from the simulated 6 m gap
  check(line::parseStationConfig(text, cfg), "setup: config parses");
  t.setStations(2);
  t.applyStationConfig(cfg);
  check(t.configAuthoritative(), "tracker reports CSV-authoritative after applyStationConfig");
  check(std::fabs(t.gap(0) - 10.0f) < 1e-3f, "gap(0) comes from CSV, not default");
  t.home();
  // Ride the full synthetic 6 m gap (reusing the `ride(...)` helper already
  // defined earlier in this file for the gap between table 1 and 2).
  ride(t, gaps[0], dirs[0] * deg, hz); // existing helper call used by the scenarios above
  check(std::fabs(t.gap(0) - 10.0f) < 1e-3f,
        "gap(0) is unchanged by the actual 6 m ride: auto-learning is disabled");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc`
Expected: compile error — `applyStationConfig`/`configAuthoritative`/`gap(int)`
(as a 10 m value) do not exist/match yet.

- [ ] **Step 3: Add the config-authoritative flag and `applyStationConfig`**

In `include/LineTracker.h`, in `class Tracker`'s public section, right
after `void setStations(int n) { ... }` (line 188):

```cpp
  // Loads a parsed config.csv as the authoritative station layout: gaps and
  // floor directions come from `cfg` and are no longer auto-learned, and
  // Wi-Fi scans stop correcting the counted station (still scanned and
  // blended into fingerprints, for the dashboard and distance log).
  // Call once, at boot, before `home()`.
  void applyStationConfig(const StationConfig &cfg) {
    setStations(cfg.count);
    for (int i = 0; i < cfg.count; ++i) {
      stationRow_[i] = cfg.row[i];
      if (i + 1 < cfg.count) {
        learned.gap[i] = cfg.row[i + 1].distanceM;
        const float dir = directionAngle(cfg.row[i + 1].direction);
        learned.gapDir[i] = dir;
        learned.gapDirKnown[i] = true;
      }
    }
    configAuthoritative_ = true;
  }
  bool configAuthoritative() const { return configAuthoritative_; }
  // Configured row for station `s`, or nullptr if no config.csv was loaded
  // or `s` is out of range. Used by the motion classifier and distance log.
  const StationRow *stationRow(int s) const {
    return configAuthoritative_ && s >= 0 && s < learned.stations ? &stationRow_[s] : nullptr;
  }
```

Add the matching private state, in the `private:` section (after line 427's
`stepCostDb`/`switchMarginDb` constants):

```cpp
  bool configAuthoritative_ = false;
  StationRow stationRow_[maxStations];
  static float directionAngle(Direction d) {
    switch (d) {
      case Direction::XPlus: return 0;
      case Direction::XMinus: return pi;
      case Direction::YPlus: return pi / 2;
      case Direction::YMinus: return -pi / 2;
    }
    return 0;
  }
```

Add `#include "StationConfig.h"` at the top of `include/LineTracker.h`
(it already includes `"SignalHistory.h"`; add the new include next to it).
`distance_m` in the CSV is "from the previous station to this one" (row
`i+1`'s `distanceM` is the gap `learned.gap[i]` between stations `i` and
`i+1`) — matches the spec's column description.

- [ ] **Step 4: Guard the auto-learn write in `finish()`**

In `finish()` (line 796-811), wrap the gap/direction learning — but not
the `station_ = to;` line, position tracking must still move with CSV
gaps too:

```cpp
      if (std::abs(to - from) == 1 && !configAuthoritative_) { // Learn this gap: spacing and floor direction.
        const int i = to > from ? from : to;
        float &g = learned.gap[i];
        g = g > 0 ? g + 0.5f * (d - g) : d;
        if (headingKnown_) {
          const float dir = to > from ? floorDir : wrap(floorDir + pi);
          if (!learned.gapDirKnown[i]) { learned.gapDir[i] = dir; learned.gapDirKnown[i] = true; }
          else learned.gapDir[i] = wrap(learned.gapDir[i] + 0.3f * wrap(dir - learned.gapDir[i]));
        }
        learnedChanged_ = true;
      }
```

(Only the `if` condition changed: `&& !configAuthoritative_` appended.)

- [ ] **Step 5: Guard the two Wi-Fi correction writes in `applyScan()`**

In `applyScan()` (lines 293-355), the two places that write `station_`
from a Wi-Fi match both need a guard — but the fingerprint blend
(`learned.fp[...]`, lines 352-354) and `score_[]` computation (line 299)
must keep running, the dashboard and distance log read those.

Block 1 (the `!known_` relocate-from-Wi-Fi-alone branch, lines 310-338):
wrap the whole block's *effect* by returning early before it, but only
when already authoritative-and-known is not the issue here — actually the
simplest correct guard is at the top of this branch:

```cpp
    if (!known_) {
      if (configAuthoritative_) return; // CSV is authoritative: never locate purely from Wi-Fi.
      // Table from Wi-Fi alone (after boot, lost samples or being carried):
      ...
```

(One line added right after `if (!known_) {`.)

Block 2 (the known-station overrule, lines 341-350):

```cpp
    else if (!configAuthoritative_ && known_ && best >= 0 && best != station_ && score_[station_] >= 0 &&
             bestCost + switchMarginDb < score_[station_]) {
```

(`!configAuthoritative_ &&` prepended to the existing condition.)

- [ ] **Step 6: Run test to verify it passes**

Run: `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc`
Expected: `all ok` including the two new checks; also re-run the existing
full scenarios (`g++ ... -DLINE_SAMPLE_HZ=1000 ...`) to confirm nothing
regressed for the non-CSV (auto-learning) path, since that path is
unchanged behaviourally — only gated by a flag that defaults to `false`.

- [ ] **Step 7: Commit**

```bash
git add include/LineTracker.h include/StationConfig.h test/line_checks.cpp
git commit -m "feat: config.csv overrides auto-learned gaps and Wi-Fi correction"
```

---

### Task 4: USB Mass Storage — `config.csv` in, `distances_last_run.csv` out

**Files:**
- Create: `src/UsbStorage.h`, `src/UsbStorage.cpp`
- Modify: `src/main.cpp` (call `UsbStorage::begin()` in `setup()` before
  `tracker.home()` is reachable; read `config.csv` and call
  `tracker.applyStationConfig()`)
- Test: manual, on-device (no synthetic FFat/USBMSC harness — see Step 5)

**Interfaces:**
- Consumes: `line::parseStationConfig` (Task 2), `Tracker::applyStationConfig`
  (Task 3), `DistanceLog::csvRow` (Task 6, produced after Task 6 lands —
  this task's writer function takes a pre-formatted `const char*` row, so
  it has no compile-time dependency on `DistanceLog`'s internals).
- Produces: `bool UsbStorage::begin()`, `bool UsbStorage::readConfig(line::StationConfig&)`,
  `void UsbStorage::appendDistanceRow(const char *csvRow)`, `bool UsbStorage::isMounted()`
  — `appendDistanceRow`/`isMounted` are consumed by Task 6.

- [ ] **Step 1: Partition and filesystem config (reuse Task 1)**

`partitions.csv` and the two `platformio.ini` lines from Task 1 Step 2 are
already in place. Nothing to add here.

- [ ] **Step 2: Write `UsbStorage.h`**

```cpp
// src/UsbStorage.h
#pragma once
#include "StationConfig.h"

namespace storage {

// Exposes an FFat partition over USB Mass Storage. `config.csv` is read
// once by `readConfig()` (call at boot, before the host could plausibly
// have mounted the drive). `appendDistanceRow()` writes directly to FFat
// and is a no-op while the host has the drive mounted (`isMounted()`):
// the device is stationary and being read when that is true, it never
// writes `distances_last_run.csv` mid-run because USB is physically
// disconnected during a run.
// ponytail: no RAM queue for writes attempted while mounted — add one if
// a write-while-mounted case ever shows up on real hardware.
bool begin();
bool readConfig(line::StationConfig &out);
void appendDistanceRow(const char *csvRow);
bool isMounted();

} // namespace storage
```

- [ ] **Step 3: Write `UsbStorage.cpp`**

```cpp
// src/UsbStorage.cpp
#include "UsbStorage.h"
#include <Arduino.h>
#include <FFat.h>
#include <USB.h>
#include <USBMSC.h>

namespace storage {
namespace {
USBMSC msc;
bool mounted = false;
constexpr const char *kConfigPath = "/config.csv";
constexpr const char *kDistancesPath = "/distances_last_run.csv";
constexpr const char *kDistancesHeader =
  "timestamp_ms,segment_type,from_station,to_station,station_id,motion_type,"
  "axis,measured_distance_m,measured_angle_deg,confidence,quality_flags\n";

// USBMSC talks in 512-byte sectors over the whole FFat-backed partition;
// FFat itself exposes files, not raw sectors, so the drive image is backed
// by a plain file on FFat acting as the exposed volume. This keeps FFat's
// own filesystem (used for config.csv/distances_last_run.csv) and the
// exposed drive separate and simple, at the cost of one extra file as the
// backing store.
// ponytail: single flat backing file via FFat, not a raw partition-block
// callback straight onto flash — simplest thing that lets USBMSC and FFat
// coexist on one partition without fighting over the FAT structures.
File backing;

int32_t onRead(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  if (!backing) return -1;
  backing.seek(lba * 512 + offset);
  return backing.read((uint8_t *)buf, size);
}
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  if (!backing) return -1;
  backing.seek(lba * 512 + offset);
  return backing.write(buf, size);
}
bool onStartStop(uint8_t power, bool start, bool eject) {
  mounted = start && !eject;
  return true;
}
} // namespace

bool begin() {
  if (!FFat.begin(true, "/ffat", 2, "ffat")) return false;
  if (!FFat.exists(kDistancesPath)) {
    File f = FFat.open(kDistancesPath, FILE_WRITE);
    if (f) { f.print(kDistancesHeader); f.close(); }
  }
  backing = FFat.open("/usbdrive.img", FILE_WRITE);
  if (!backing) return false;
  const uint32_t sectors = backing.size() / 512;
  msc.vendorID("WIBROMTR");
  msc.productID("SKID LOG");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  msc.begin(sectors, 512);
  USB.begin();
  return true;
}

bool readConfig(line::StationConfig &out) {
  if (!FFat.exists(kConfigPath)) return false;
  File f = FFat.open(kConfigPath, FILE_READ);
  if (!f) return false;
  char buf[2048];
  const size_t n = f.read((uint8_t *)buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  return line::parseStationConfig(buf, out);
}

void appendDistanceRow(const char *csvRow) {
  if (mounted) return; // Host owns the drive right now; device is parked.
  File f = FFat.open(kDistancesPath, FILE_APPEND);
  if (!f) return;
  f.print(csvRow);
  f.close();
}

bool isMounted() { return mounted; }

} // namespace storage
```

(The `usbdrive.img` backing-file approach in `onRead`/`onWrite` is a known
pattern for exposing an Arduino-ESP32 filesystem through `USBMSC` without a
second raw partition; Step 5's on-device check validates the exact
`USBMSC` method names against whatever the pinned core ships — same note
as Task 1 Step 3.)

- [ ] **Step 4: Wire into `main.cpp`**

In `src/main.cpp`'s `setup()`, after display/MPU6050 init and before the
first call that could reach `tracker.home()` (the BOOT-button handler),
add:

```cpp
#include "UsbStorage.h"
// ...
if (storage::begin()) {
  line::StationConfig cfg;
  if (storage::readConfig(cfg)) {
    tracker.applyStationConfig(cfg);
    Serial.println("config.csv: loaded, CSV is authoritative");
  } else {
    Serial.println("config.csv: not found or invalid, auto-learning as before");
  }
} else {
  Serial.println("USB storage: FFat.begin failed");
}
```

- [ ] **Step 5: On-device check (no synthetic harness — this is hardware I/O)**

1. Flash, connect over USB. Confirm Serial prints one of the three lines
   above (not a crash/hang).
2. Without a `config.csv` present: confirm the drive still appears on the
   host as a USB drive (`lsusb`/Windows Explorer), and the firmware logs
   "not found or invalid" and behaves exactly as before (auto-learning).
3. Create `config.csv` on the host matching the Task 2 example, eject,
   power-cycle the board: confirm Serial logs "loaded, CSV is authoritative"
   and (via the existing web dashboard's "Odstępy wyuczone" fact, Spec B)
   gaps no longer change after real rides.
4. Ride the line once, reconnect USB: confirm `distances_last_run.csv`
   exists and is non-empty (header + rows once Task 6 lands — until then,
   just the header).

This step has no automated check because it is exercising real USB
enumeration and flash I/O; it is still the one required check for this
task's non-trivial logic (`onRead`/`onWrite`/`isMounted` gating), per the
"lazy code without its check is unfinished" rule, done on hardware instead
of host because there is no USB host simulator in this toolchain
(ladder rung 1: don't build one for a single task).

- [ ] **Step 6: Commit**

```bash
git add src/UsbStorage.h src/UsbStorage.cpp src/main.cpp
git commit -m "feat: USB Mass Storage exposes config.csv in, distances_last_run.csv out"
```

---

### Task 5: `MotionClassifier` — technological vs transport

**Files:**
- Create: `include/MotionClassifier.h`
- Test: `test/motion_classifier_checks.cpp`

**Interfaces:**
- Consumes: `line::StationRow`, `line::Motion` from Task 2.
- Produces: `line::MotionResult`, `line::classifyMotion(const StationRow&, float distanceM, float angleDeg)`
  — consumed by Task 6 (`DistanceLog`, formats a `MotionResult` into a CSV row)
  and by the `Tracker.finish()` call site wired in this task's Step 4.

- [ ] **Step 1: Write the failing test**

```cpp
// test/motion_classifier_checks.cpp
// g++ -std=c++17 -Iinclude test/motion_classifier_checks.cpp -o mcc && ./mcc
#include <cmath>
#include <cstdio>
#include "MotionClassifier.h"
#include "StationConfig.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}

int main() {
  using namespace line;

  {
    StationRow none;
    none.motion = Motion::None;
    const MotionResult r = classifyMotion(none, 0.3f, 0);
    check(!r.valid, "station with motion=none produces no classification");
  }
  {
    StationRow lift;
    lift.motion = Motion::Lift;
    lift.motionRange = 0.4f;
    const MotionResult r = classifyMotion(lift, 0.38f, 0);
    check(r.valid, "lift within range classified");
    check(std::fabs(r.measuredDistanceM - 0.38f) < 1e-6f, "measured distance recorded");
    check(!r.overRange, "within 1.5x range is not flagged");
  }
  {
    StationRow lift;
    lift.motion = Motion::Lift;
    lift.motionRange = 0.4f;
    const MotionResult r = classifyMotion(lift, 0.9f, 0); // 2.25x configured range
    check(r.valid, "lift still classified when it exceeds the configured range");
    check(r.overRange, "exceeding 1.5x range is flagged, not silently reclassified");
  }
  {
    StationRow rot;
    rot.motion = Motion::RotationZ;
    rot.motionRange = 90;
    const MotionResult r = classifyMotion(rot, 0, 91.0f);
    check(r.valid, "rotation classified");
    check(std::fabs(r.measuredAngleDeg - 91.0f) < 1e-6f, "measured angle recorded, not distance");
    check(std::fabs(r.measuredDistanceM) < 1e-6f, "distance field unused for rotation");
    check(!r.overRange, "91 deg vs 90 deg configured is within 1.5x");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Iinclude test/motion_classifier_checks.cpp -o mcc`
Expected: compile error, `MotionClassifier.h` does not exist.

- [ ] **Step 3: Write the implementation**

```cpp
// include/MotionClassifier.h
#pragma once
#include <cmath>
#include "StationConfig.h"

namespace line {

// One technological motion observed at a station, classified from
// measurements the Tracker already produces for a Parked -> Moving ->
// Parked cycle that did NOT change the counted station (see
// Tracker::finish(), Task 3). Never influences which station is current —
// that is a hard requirement from the spec, enforced by the caller never
// writing `station_` from this result, not by anything in here.
struct MotionResult {
  bool valid = false;
  Motion motion = Motion::None;
  float measuredDistanceM = 0; // Lift / z_transfer / compound.
  float measuredAngleDeg = 0;  // RotationZ.
  bool overRange = false;      // Measured value exceeds 1.5x the configured range.
};

// `distanceM`/`angleDeg` are whichever of the two the caller measured for
// this cycle (the other is 0) — the Tracker knows which one applies from
// `row.motion` before calling this, same as this function does.
inline MotionResult classifyMotion(const StationRow &row, float distanceM, float angleDeg) {
  MotionResult r;
  if (row.motion == Motion::None) return r;
  r.valid = true;
  r.motion = row.motion;
  const bool isRotation = row.motion == Motion::RotationZ;
  r.measuredDistanceM = isRotation ? 0 : distanceM;
  r.measuredAngleDeg = isRotation ? angleDeg : 0;
  const float measured = isRotation ? angleDeg : distanceM;
  r.overRange = row.motionRange > 0 && std::fabs(measured) > 1.5f * row.motionRange;
  return r;
}

} // namespace line
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Iinclude test/motion_classifier_checks.cpp -o mcc && ./mcc`
Expected: `all ok`.

- [ ] **Step 5: Wire the call site into `Tracker::finish()`**

In `include/LineTracker.h`'s `finish()` (same area as Task 3 Step 4), add
right after the existing `if (ride && known_) { ... }` block (after its
closing `}` on line 811, before `if (ride || carried) {` on line 812):

```cpp
    MotionResult motionResult; // Default: invalid, nothing technological happened.
    if (!ride && !carried && known_ && moved) {
      if (const StationRow *row = stationRow(station_)) {
        // Same station before and after: not a transport ride. `ph.z` is the
        // vertical displacement already separated out of the horizontal `d`
        // (see the `flat()`/`norm(ph)` split above); `turnSoFar()` is the
        // heading change integrated since the last stand, in radians.
        motionResult = classifyMotion(*row, p.z, turnSoFar() * 180 / pi);
      }
    }
```

`#include "MotionClassifier.h"` next to the `StationConfig.h` include
added in Task 3. `motionResult` is consumed by Task 6's Step 5 (it is a
local variable here on purpose — Task 6 extends this same `finish()` edit
rather than re-deriving the measurement).
`# ponytail: p.z as "vertical displacement" and turnSoFar() as "rotation
amount" are the two most direct signals finish() already computes for
this; validate against a real lift/turntable recording in Task 4 Step 5's
on-device check before trusting the numbers, that's what motionRange is
the calibration knob for.`

- [ ] **Step 6: Run the full line test suite to confirm no regression**

Run: `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc`
Expected: `all ok` — this task only adds a new branch that fires when
`!ride && !carried`, which the existing scenarios' transit-counting checks
don't exercise, so they should be unaffected.

- [ ] **Step 7: Commit**

```bash
git add include/MotionClassifier.h include/LineTracker.h test/motion_classifier_checks.cpp
git commit -m "feat: classify technological motion without changing the station"
```

---

### Task 6: `DistanceLog` — `distances_last_run.csv` rows

**Files:**
- Create: `include/DistanceLog.h`
- Modify: `include/LineTracker.h` (`finish()`: format and emit a row)
- Modify: `src/main.cpp` (call `storage::appendDistanceRow` when `Tracker`
  reports a new row)
- Test: `test/distance_log_checks.cpp`

**Interfaces:**
- Consumes: `line::MotionResult` (Task 5), `line::Transit` fields already
  in `LineTracker.h`, `storage::appendDistanceRow` (Task 4).
- Produces: `line::formatDistanceRow(...)` (pure formatting function,
  testable off-device), `Tracker::newDistanceRow()` /
  `Tracker::distanceRowCsv()` (mirrors the existing `newTransit()` /
  transit-array pattern already in `Tracker`) — consumed by `main.cpp`'s
  loop.

- [ ] **Step 1: Write the failing test**

```cpp
// test/distance_log_checks.cpp
// g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc && ./dlc
#include <cstdio>
#include <cstring>
#include "DistanceLog.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}

int main() {
  using namespace line;

  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 1234, "transport", 0, 1, "", Motion::None, "", 10.2f, 0, 0.9f, "");
    check(std::strstr(buf, "1234,transport,0,1,,,,10.200,,0.90,") != nullptr,
          "transport row has from/to, no station_id/motion/angle");
  }
  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 5678, "technological", -1, -1, "ST02", Motion::Lift, "Z", 0.38f, 0, 1.0f, "");
    check(std::strstr(buf, "5678,technological,,,ST02,lift,Z,0.380,,1.00,") != nullptr,
          "technological row has station_id/motion/axis, no from/to");
  }
  {
    char buf[200];
    formatDistanceRow(buf, sizeof(buf), 9, "technological", -1, -1, "ST03", Motion::RotationZ, "Z", 0, 91.0f, 1.0f, "over_range");
    check(std::strstr(buf, ",,91.000,1.00,over_range\n") != nullptr,
          "rotation row carries angle not distance, and the quality flag");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc`
Expected: compile error, `DistanceLog.h` does not exist.

- [ ] **Step 3: Write the implementation**

```cpp
// include/DistanceLog.h
#pragma once
#include <cstdio>
#include "MotionClassifier.h"

namespace line {

inline const char *motionName(Motion m) {
  switch (m) {
    case Motion::None: return "";
    case Motion::Lift: return "lift";
    case Motion::RotationZ: return "rotation_z";
    case Motion::ZTransfer: return "z_transfer";
    case Motion::Compound: return "compound";
  }
  return "";
}

// Formats one row of distances_last_run.csv into `buf` (NUL-terminated,
// truncated if `bufSize` is too small — callers use a fixed stack buffer
// sized for the longest possible row, see Tracker::distanceRowCsv()).
// Columns: timestamp_ms,segment_type,from_station,to_station,station_id,
// motion_type,axis,measured_distance_m,measured_angle_deg,confidence,quality_flags
inline void formatDistanceRow(char *buf, size_t bufSize, uint32_t timestampMs,
                               const char *segmentType, int from, int to,
                               const char *stationId, Motion motion, const char *axis,
                               float measuredDistanceM, float measuredAngleDeg,
                               float confidence, const char *qualityFlags) {
  char fromStr[4] = "", toStr[4] = "";
  if (from >= 0) std::snprintf(fromStr, sizeof(fromStr), "%d", from);
  if (to >= 0) std::snprintf(toStr, sizeof(toStr), "%d", to);
  char distStr[16] = "", angleStr[16] = "";
  if (measuredDistanceM != 0 || motion == Motion::None)
    std::snprintf(distStr, sizeof(distStr), "%.3f", measuredDistanceM);
  if (measuredAngleDeg != 0) std::snprintf(angleStr, sizeof(angleStr), "%.3f", measuredAngleDeg);
  std::snprintf(buf, bufSize, "%u,%s,%s,%s,%s,%s,%s,%s,%s,%.2f,%s\n",
                (unsigned)timestampMs, segmentType, fromStr, toStr, stationId,
                motionName(motion), axis, distStr, angleStr, confidence, qualityFlags);
}

} // namespace line
```

(The test's expectation for the pure-transport row includes
`measuredDistanceM` even though `motion == Motion::None` — matches the
`from`/`to` transport case where `distStr` should still show the ride
distance; the `|| motion == Motion::None` clause in the `if` above exists
for exactly that case. For a technological row with `measuredDistanceM ==
0` genuinely meaning "unset" — e.g. the rotation example — the field is
correctly left blank since `motion != Motion::None` and the value is 0.)

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc && ./dlc`
Expected: `all ok`.

- [ ] **Step 5: Emit a row from `Tracker::finish()`**

Continue the Task 5 Step 5 edit in `finish()`. Add a small ring buffer
(mirrors the existing `transits_`/`transitHead_`/`transitCount_` pattern)
next to those fields in the `private:` section:

```cpp
  char distanceRow_[200] = {};
  bool newDistanceRow_ = false;
```

And two public accessors next to `newTransit()`:

```cpp
  bool newDistanceRow() { const bool r = newDistanceRow_; newDistanceRow_ = false; return r; }
  const char *distanceRowCsv() const { return distanceRow_; }
```

In `finish()`, after the `motionResult` block from Task 5 Step 5 and
before `if (ride || carried) {` (line 812), add:

```cpp
    if (ride && !carried) {
      formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000),
                         "transport", knownBefore ? from : -1, known_ ? station_ : -1,
                         "", Motion::None, "", d, 0, slow_ ? 1.0f : 0.5f, "");
      newDistanceRow_ = true;
    } else if (motionResult.valid) {
      const StationRow *row = stationRow(station_);
      formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000),
                         "technological", -1, -1, row ? row->id : "", motionResult.motion,
                         "Z", motionResult.measuredDistanceM, motionResult.measuredAngleDeg,
                         1.0f, motionResult.overRange ? "over_range" : "");
      newDistanceRow_ = true;
    }
```

- [ ] **Step 6: Wire `main.cpp`'s loop to flush rows to USB storage**

In `src/main.cpp`'s main loop, wherever the existing `tracker.newTransit()`
check already lives (it polls once per loop iteration), add right next to
it:

```cpp
if (tracker.newDistanceRow()) {
  storage::appendDistanceRow(tracker.distanceRowCsv());
}
```

- [ ] **Step 7: Run full suite, including line_checks, to confirm no regression**

Run:
```text
g++ -std=c++17 -Iinclude test/station_config_checks.cpp -o scc && ./scc
g++ -std=c++17 -Iinclude test/motion_classifier_checks.cpp -o mcc && ./mcc
g++ -std=c++17 -Iinclude test/distance_log_checks.cpp -o dlc && ./dlc
g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc
g++ -std=c++17 -O2 -Iinclude test/displacement_checks.cpp -o dc && ./dc
g++ -std=c++17 -Iinclude -fsyntax-only test/signal_checks.cpp
```
Expected: all report `all ok` / pass, zero failures.

- [ ] **Step 8: Commit**

```bash
git add include/DistanceLog.h include/LineTracker.h src/main.cpp test/distance_log_checks.cpp
git commit -m "feat: write distances_last_run.csv rows for transport and technological motion"
```

## Deferred (explicit, not in this plan)

Reference-point / anchor-sequence recognition (`reference_point`,
`motion_signature` columns from the spec's notes) — separate plan, after
this one ships and Task 4 Step 5's on-device check confirms the flash
budget and config loading actually work on real hardware.
