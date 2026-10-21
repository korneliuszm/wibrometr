#include "BleDistance.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cmath>

namespace ble {

namespace {
constexpr const char *kNamespace = "ble";
} // namespace

void BleDistance::begin() {
  Address target;
  Address::parse(BLE_TARGET_ADDRESS, target);
  float rssiAt1m = kDefaultRssiAt1mDbm;
  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    if (prefs.getBytesLength("target") == sizeof(Address)) prefs.getBytes("target", &target, sizeof(Address));
    rssiAt1m = prefs.getFloat("rssi1m", kDefaultRssiAt1mDbm);
    prefs.end();
  }
  tracker_.setTarget(target);
  tracker_.setRssiAt1m(rssiAt1m);
  char text[18];
  target.format(text, sizeof text);
  Serial.printf("BLE: target %s%s, RSSI at 1 m %.0f dBm\n", text,
                target.isResolvablePrivate() ? " (private address, may rotate)" : "", rssiAt1m);
  running_ = scanner_.begin();
}

void BleDistance::save() const {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBytes("target", &tracker_.target(), sizeof(Address));
  prefs.putFloat("rssi1m", tracker_.rssiAt1m());
  prefs.end();
}

void BleDistance::loop(uint32_t now) {
  if (!running_) return;
  Advert a;
  for (int i = 0; i < 64 && scanner_.pop(a); ++i) tracker_.add(a);
  if (int32_t(now - nextLogMs_) < 0) return;
  nextLogMs_ = now + 1000;
  const float d = tracker_.distance(now);
  if (std::isfinite(d))
    Serial.printf("BLE: rssi=%d median=%.1f dBm distance~%.2f m interval~%lu ms nearby=%u drops=%lu\n",
                  tracker_.lastRssi(), tracker_.smoothedRssi(now), d,
                  (unsigned long)tracker_.advertIntervalMs(), unsigned(tracker_.candidateCount(now)),
                  (unsigned long)scanner_.dropped());
  else
    Serial.printf("BLE: target not heard%s, nearby=%u\n", tracker_.seen() ? " recently" : " yet",
                  unsigned(tracker_.candidateCount(now)));
}

bool BleDistance::adoptStrongest(uint32_t now, Address &adopted) {
  Candidate c;
  if (!tracker_.strongest(now, c)) return false;
  tracker_.setTarget(c.addr);
  adopted = c.addr;
  save();
  return true;
}

bool BleDistance::calibrate1m(uint32_t now, float &rssiAt1m) {
  if (!tracker_.fresh(now)) return false;
  rssiAt1m = tracker_.smoothedRssi(now);
  tracker_.setRssiAt1m(rssiAt1m);
  save();
  return true;
}

DistanceView BleDistance::view(uint32_t now) const {
  DistanceView v;
  v.scanning = running_ && scanner_.scanning();
  v.target = tracker_.target();
  v.seen = tracker_.seen();
  v.fresh = tracker_.fresh(now);
  v.apple = tracker_.targetApple();
  v.ageMs = tracker_.ageMs(now);
  v.distanceM = tracker_.distance(now);
  v.rssiMedian = tracker_.smoothedRssi(now);
  v.rssiLast = tracker_.lastRssi();
  v.intervalMs = tracker_.advertIntervalMs();
  v.rssiAt1m = tracker_.rssiAt1m();
  v.haveCandidate = tracker_.strongest(now, v.candidate);
  v.nearbyCount = tracker_.candidateCount(now);
  return v;
}

} // namespace ble
