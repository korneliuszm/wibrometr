#include "BleScanner.h"
#include <Arduino.h>
#include <atomic>
#include <cstring>
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

// The Arduino core frees the Bluetooth controller memory at boot unless
// btInUse() says otherwise; its weak default returns false.
extern "C" bool btInUse(void) { return true; }

namespace ble {

namespace {
QueueHandle_t queue = nullptr;
std::atomic<uint32_t> droppedCount{0};
std::atomic<bool> active{false};
uint8_t ownAddrType = 0;

// Apple adverts carry manufacturer-specific data (AD type 0xFF) with company
// identifier 0x004C, little endian.
bool hasAppleData(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i + 1 < len;) {
    const uint8_t fieldLen = data[i];
    if (!fieldLen || i + 1 + fieldLen > len) break;
    if (data[i + 1] == 0xFF && fieldLen >= 3 && data[i + 2] == 0x4C && data[i + 3] == 0x00) return true;
    i += 1 + fieldLen;
  }
  return false;
}

void report(const ble_addr_t &addr, int8_t rssi, const uint8_t *data, uint8_t len) {
  if (rssi == 127 || !queue) return; // 127: RSSI unavailable.
  Advert a;
  for (int i = 0; i < 6; ++i) a.addr.b[i] = addr.val[5 - i]; // NimBLE stores LSB first.
  a.rssi = rssi;
  a.apple = hasAppleData(data, len);
  a.ms = millis();
  if (xQueueSend(queue, &a, 0) != pdTRUE) ++droppedCount;
}

void startScan();

int onGapEvent(struct ble_gap_event *event, void *) {
  switch (event->type) {
  case BLE_GAP_EVENT_DISC:
    report(event->disc.addr, event->disc.rssi, event->disc.data, event->disc.length_data);
    return 0;
#if MYNEWT_VAL(BLE_EXT_ADV)
  case BLE_GAP_EVENT_EXT_DISC:
    report(event->ext_disc.addr, event->ext_disc.rssi, event->ext_disc.data, event->ext_disc.length_data);
    return 0;
#endif
  case BLE_GAP_EVENT_DISC_COMPLETE:
    active = false;
    startScan(); // Scanning runs forever; restart if the stack ended it.
    return 0;
  default:
    return 0;
  }
}

void startScan() {
  struct ble_gap_disc_params params = {};
  params.itvl = 0x50;   // 50 ms (units of 0.625 ms); window == interval: listen
  params.window = 0x50; // continuously, so no advert slot is skipped.
  params.passive = 1;   // Only listen; never send scan requests.
  params.filter_duplicates = 0; // Every advert is an RSSI sample.
  const int rc = ble_gap_disc(ownAddrType, BLE_HS_FOREVER, &params, onGapEvent, nullptr);
  active = rc == 0;
  if (rc) Serial.printf("BLE: scan start failed (%d)\n", rc);
}

void onSync() {
  if (ble_hs_id_infer_auto(0, &ownAddrType) != 0) ownAddrType = BLE_OWN_ADDR_PUBLIC;
  startScan();
}

void onReset(int reason) {
  active = false;
  Serial.printf("BLE: host reset (%d)\n", reason);
}

void hostTask(void *) {
  nimble_port_run(); // Returns only after nimble_port_stop().
  nimble_port_freertos_deinit();
}
} // namespace

bool BleScanner::begin() {
  if (queue) return true;
  queue = xQueueCreate(64, sizeof(Advert));
  if (!queue) return false;
  const esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    Serial.printf("BLE: nimble_port_init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  nimble_port_freertos_init(hostTask);
  return true;
}

bool BleScanner::pop(Advert &out) { return queue && xQueueReceive(queue, &out, 0) == pdTRUE; }
uint32_t BleScanner::dropped() const { return droppedCount; }
bool BleScanner::scanning() const { return active; }

} // namespace ble
