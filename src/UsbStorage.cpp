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
  (void)power;
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
