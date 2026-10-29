// src/UsbStorage.cpp
#include "UsbStorage.h"
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <atomic>
#include <cstdio>
#include "esp_vfs_fat.h"
#include "ff.h"
#include "diskio.h"
#include "diskio_wl.h"

namespace storage {
namespace {
USBMSC msc;
// Written from the USB MSC onStartStop callback, read from the main loop;
// those can run on either core on the dual-core S3. A single aligned bool
// read/write is not torn on this architecture, but the two sides still need
// a memory-ordering guarantee the plain bool doesn't give -- std::atomic is
// a one-line fix with no measurable cost here, so just use it instead of
// arguing about whether the race is benign.
std::atomic<bool> mounted{false};
constexpr const char *kBasePath = "/ffat";
constexpr const char *kPartitionLabel = "ffat";
constexpr const char *kConfigPath = "/ffat/config.csv";
constexpr const char *kDistancesPath = "/ffat/distances_last_run.csv";
constexpr const char *kDistancesHeader =
  "timestamp_ms,segment_type,from_station,to_station,station_id,motion_type,"
  "axis,measured_distance_m,measured_angle_deg,confidence,quality_flags\n";

// There used to be a separate "/usbdrive.img" file on this same FFat
// partition, exposed to USB MSC as a nested FAT image, while config.csv /
// distances_last_run.csv were plain files on FFat's OWN filesystem. Those
// are two unrelated filesystems: a config.csv the host writes into the
// nested image was never visible to firmware's FFat.open("/config.csv"),
// and vice versa -- and the image file started out zero bytes long (never
// sized), so USB MSC reported a zero-sector drive to the host. Both bugs
// came from the same root cause: a second filesystem that didn't need to
// exist.
//
// Fix: mount the "ffat" partition ourselves (instead of through Arduino's
// FFat wrapper, which does not expose the handle we need) and get the
// wear-levelling handle. config.csv / distances_last_run.csv are then
// plain POSIX files on the "/ffat" VFS mount, same as before. USB MSC's
// onRead/onWrite call disk_read()/disk_write() -- the exact low-level
// FatFs driver functions registered for that same handle by
// esp_vfs_fat_spiflash_mount() and used internally for every file
// read/write -- so the sectors the host sees ARE this filesystem, not a
// copy of it. No new library: esp_vfs_fat.h/ff.h/diskio*.h are already
// part of the esp32-arduino core (Arduino's own FFat class is a thin
// wrapper over the same ESP-IDF calls).
// ponytail: still unverified on real hardware/a real USB host OS (no
// ESP32 available in this environment) -- verified here by `pio run`
// (compiles, links, flash-size sane) and by inspecting the ESP-IDF
// header contracts (disk_read/disk_write/disk_ioctl, wl_handle_t) that
// esp_vfs_fat_spiflash_mount()/Arduino's FFat already rely on for the
// exact same partition. Assumes the host/TinyUSB side always issues
// sector-aligned transfers (offset 0, size a multiple of the FatFs
// sector size) -- true in every ESP32 USBMSC example seen, but not
// provable without a real host plugged in. The unmount/remount on eject
// in onStartStop() (drops FatFs's stale "win" sector cache after the
// host rewrites sectors behind its back) is equally unverified on real
// hardware for the same reason.
wl_handle_t wlHandle = WL_INVALID_HANDLE;
BYTE pdrv = 0xFF;
WORD sectorSize = 512;

int32_t onRead(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  if (pdrv == 0xFF || offset != 0 || sectorSize == 0 || size % sectorSize != 0) return -1;
  return disk_read(pdrv, (BYTE *)buf, lba, size / sectorSize) == RES_OK ? (int32_t)size : -1;
}
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  if (pdrv == 0xFF || offset != 0 || sectorSize == 0 || size % sectorSize != 0) return -1;
  return disk_write(pdrv, (const BYTE *)buf, lba, size / sectorSize) == RES_OK ? (int32_t)size : -1;
}
bool onStartStop(uint8_t power, bool start, bool eject) {
  (void)power;
  const bool wasMounted = mounted.load(std::memory_order_relaxed);
  const bool nowMounted = start && !eject;
  mounted.store(nowMounted, std::memory_order_relaxed);

  // Host just released the drive (mounted -> unmounted): firmware is about
  // to resume readConfig()/appendDistanceRow(). FatFs keeps a per-volume
  // "win" sector cache in ff.c, filled by the LAST sector *we* touched --
  // but onRead/onWrite went straight through disk_read()/disk_write() via
  // ff_diskio_get_pdrv_wl(), bypassing that cache entirely. If the host
  // rewrote the same sector(s) through raw MSC writes, the cache can still
  // hold the pre-eject content. Dropping and remounting the volume is the
  // only way to flush that cache without reaching into ff.c internals.
  if (wasMounted && !nowMounted) {
    esp_vfs_fat_spiflash_unmount_rw_wl(kBasePath, wlHandle);
    esp_vfs_fat_mount_config_t conf = {};
    conf.format_if_mount_failed = true;
    conf.max_files = 4;
    conf.allocation_unit_size = 0;
    if (esp_vfs_fat_spiflash_mount(kBasePath, kPartitionLabel, &conf, &wlHandle) == ESP_OK) {
      pdrv = ff_diskio_get_pdrv_wl(wlHandle);
    } else {
      pdrv = 0xFF; // Remount failed: park onRead/onWrite rather than touch a half-mounted volume.
    }
  }
  return true;
}
} // namespace

bool begin() {
  esp_vfs_fat_mount_config_t conf = {};
  conf.format_if_mount_failed = true;
  conf.max_files = 4;
  conf.allocation_unit_size = 0; // Default for the wear-levelling layer.
  if (esp_vfs_fat_spiflash_mount(kBasePath, kPartitionLabel, &conf, &wlHandle) != ESP_OK) return false;

  pdrv = ff_diskio_get_pdrv_wl(wlHandle);
  DWORD sectorCount = 0;
  if (pdrv == 0xFF || disk_ioctl(pdrv, GET_SECTOR_COUNT, &sectorCount) != RES_OK || sectorCount == 0) return false;
  WORD ss = 512;
  if (disk_ioctl(pdrv, GET_SECTOR_SIZE, &ss) == RES_OK && ss > 0) sectorSize = ss;

  if (FILE *f = fopen(kDistancesPath, "r")) {
    fclose(f);
  } else if (FILE *f = fopen(kDistancesPath, "w")) {
    fputs(kDistancesHeader, f);
    fclose(f);
  }

  msc.vendorID("WIBROMTR");
  msc.productID("SKID LOG");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  msc.begin(sectorCount, sectorSize);
  USB.begin();
  return true;
}

bool readConfig(line::StationConfig &out) {
  FILE *f = fopen(kConfigPath, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  const long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);
  char buf[2048];
  // A file bigger than the buffer would otherwise be silently truncated
  // (the trailing stations just disappear with no error): reject it
  // outright instead, same as any other invalid config.
  if (fileSize < 0 || (size_t)fileSize > sizeof(buf) - 1) { fclose(f); return false; }
  const size_t n = fread(buf, 1, (size_t)fileSize, f);
  fclose(f);
  if (n != (size_t)fileSize) return false; // Read came up short of the file's own size.
  buf[n] = 0;
  return line::parseStationConfig(buf, out);
}

void appendDistanceRow(const char *csvRow) {
  if (mounted.load(std::memory_order_relaxed)) return; // Host owns the drive right now; device is parked.
  FILE *f = fopen(kDistancesPath, "a");
  if (!f) return;
  fputs(csvRow, f);
  fclose(f);
}

bool isMounted() { return mounted.load(std::memory_order_relaxed); }

} // namespace storage
