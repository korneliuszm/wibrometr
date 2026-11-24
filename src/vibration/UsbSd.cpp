// UNVERIFIED ON HARDWARE (see the checklist in docs, Task 8):
//  - host detection: tud_mounted() && !tud_suspended() as "host present";
//  - msc.begin(SD.numSectors(), SD.sectorSize()) with the card's real sector count
//    (whole raw card, partition table included);
//  - the busy/retry behaviour while the writer is still parking.
// Compiled against arduino-esp32 3.3.6 (TinyUSB 0.20) only; never linked here.
//
// Threading: onRead/onWrite/onStartStop run on the TinyUSB task ("usbd",
// priority configMAX_PRIORITIES - 1, not pinned). They only touch atomics
// (UsbOwnership, MeasWriter::parked()) and, once the writer is parked, the card.
// service() runs on the main loop.
#include "UsbSd.h"
#include <Arduino.h>
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>
// tud_mounted / tud_suspended. On the framework include path (arduino_tinyusb);
// its cdc_host.h trips -Wmissing-field-initializers, not our code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "tusb.h"
#pragma GCC diagnostic pop
#include "CardGate.h"

namespace usbsd {
namespace {

USBMSC msc;
UsbOwnership *own_ = nullptr;
MeasWriter *writer_ = nullptr;
// Set once in begin() before USB.begin() creates the USB task; read-only afterwards.
uint32_t sectors_ = 0;
uint16_t sectorSize_ = 512;
uint32_t lastServiceMs_ = 0;
bool serviced_ = false;
bool active_ = false;  // MSC drive started; main loop only.
bool diskMode_ = false;  // The host may see the card; main loop only.

CardGate gate;  // USB task: enter/leave; main loop: release/grant.

// Returns true when the card may be used for this transfer; the caller then
// must call gate.leave() when done (every return path). Any sector access is
// reported first: after an eject it hands the card back to the host, which
// makes the main loop park the writer (calling onAccess only once parked would
// leave a host that re-accesses after an eject retrying forever, because the
// writer is never parked again). The gate is entered before parked() is read,
// so once release() has returned true the main loop may unpark: no transfer is
// running and every later one is refused. While the card is not ours (writer
// not parked, or gate revoked) the caller returns TUD_MSC_RET_BUSY (0) and
// TinyUSB re-invokes it with the same parameters. That retry is immediate, and
// this task has the highest priority, so yield 1 tick first: without it the
// retry loop would starve the main loop or the writer task, which are the ones
// that have to finish the park or the release.
bool cardReady() {
  own_->onAccess();
  if (gate.enter()) {
    if (writer_->parked()) return true;
    gate.leave();
  }
  vTaskDelay(1);
  return false;
}

// Sector-aligned transfers only: offset != 0 happens in TinyUSB only when its
// buffer (CONFIG_TINYUSB_MSC_BUFSIZE = 4096) is smaller than a block, which a
// 512 B card never needs; such a request is rejected as an error.
bool validRange(uint32_t lba, uint32_t offset, uint32_t size, uint32_t &count) {
  if (offset != 0 || size == 0 || size % sectorSize_ != 0) return false;
  count = size / sectorSize_;
  return lba < sectors_ && count <= sectors_ - lba;
}

int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  uint32_t count = 0;
  if (!validRange(lba, offset, size, count)) return -1;
  if (!cardReady()) return 0;
  uint8_t *p = static_cast<uint8_t *>(buffer);
  uint32_t i = 0;
  for (; i < count; ++i, p += sectorSize_) {
    if (!SD.readRAW(p, lba + i)) break;
  }
  gate.leave();
  return i == count ? int32_t(size) : -1;
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  uint32_t count = 0;
  if (!validRange(lba, offset, size, count)) return -1;
  if (!cardReady()) return 0;
  uint8_t *p = buffer;
  uint32_t i = 0;
  for (; i < count; ++i, p += sectorSize_) {
    if (!SD.writeRAW(p, lba + i)) break;
  }
  gate.leave();
  return i == count ? int32_t(size) : -1;
}

bool onStartStop(uint8_t power, bool start, bool eject) {
  (void)power;
  (void)start;
  if (eject) own_->onEject();
  return true;
}

bool hostPresent() { return tud_mounted() && !tud_suspended(); }

} // namespace

bool begin(UsbOwnership &own, MeasWriter &writer) {
  for (int i = 0; i < 40 && writer.status().state == MeasStatus::State::NoCard; ++i) delay(50);

  // State is not NoCard once SD.begin succeeded; an Error state (e.g. /MEAS)
  // still has the card mounted. A zero count means it was dropped again.
  const size_t sectors = writer.status().state != MeasStatus::State::NoCard ? SD.numSectors() : 0;
  const size_t sectorSize = sectors ? SD.sectorSize() : 0;
  bool card = sectors > 0 && sectors <= UINT32_MAX && sectorSize > 0 && sectorSize <= 4096;
  if (card) {
    own_ = &own;
    writer_ = &writer;
    sectors_ = uint32_t(sectors);
    sectorSize_ = uint16_t(sectorSize);
    msc.vendorID("WIBROMTR");
    msc.productID("VIBRACJE");
    msc.productRevision("1.0");
    msc.onRead(onRead);
    msc.onWrite(onWrite);
    msc.onStartStop(onStartStop);
    msc.mediaPresent(false);  // An empty reader until disk mode is switched on.
    card = msc.begin(sectors_, sectorSize_);
    // Without the drive the host never owns anything: service() stays idle and
    // the recorder keeps the card even while a computer is connected.
    active_ = card;
  }
  USB.begin();  // Always: the serial console runs over the same USB port.
  Serial.printf("USB: %s\n", card ? "karta SD jako dysk (po wlaczeniu na ekranie)" : "bez karty, tylko port szeregowy");
  return card;
}

void service(uint32_t nowMs) {
  if (!active_ || (serviced_ && nowMs - lastServiceMs_ < 100)) return;
  serviced_ = true;
  lastServiceMs_ = nowMs;
  const bool host = hostPresent();
  // Cable pulled (not merely suspended, e.g. the computer asleep): the next connection starts as serial again.
  if (!tud_mounted() && diskMode_) setDiskMode(false);
  own_->setEnumerated(host && diskMode_);
}

bool hostConnected() { return active_ && hostPresent(); }

bool diskMode() { return diskMode_; }

void setDiskMode(bool on) {
  if (!active_ || on == diskMode_) return;
  if (on && !hostPresent()) return;
  diskMode_ = on;
  msc.mediaPresent(on);
  Serial.printf("USB: tryb dysku %s\n", on ? "wlaczony" : "wylaczony");
}

bool release() { return !active_ || gate.release(); }

void grant() { gate.grant(); }

} // namespace usbsd
