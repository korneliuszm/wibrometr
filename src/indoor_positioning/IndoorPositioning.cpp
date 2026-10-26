#include "IndoorPositioning.h"
#include <Arduino.h>
#include <cmath>

#if IPS_DEBUG
#define IPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define IPS_LOG(...) do {} while (0)
#endif

namespace ips {

namespace {
// Equilateral triangle A(0,0), B(s,0), C(s/2, s*sqrt(3)/2).
void trianglePoint(float side, uint8_t index, float &x, float &y) {
  x = index == 0 ? 0 : index == 1 ? side : side / 2;
  y = index == 2 ? side * 0.8660254f : 0;
}
#if IPS_DEBUG
void printBssid(const Bssid &b) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", b.b[0], b.b[1], b.b[2], b.b[3], b.b[4], b.b[5]);
}
#endif
} // namespace

void IndoorPositioning::begin() {
  calibrated_ = loadCalibration(cal_);
  Serial.printf("IPS: calibration %s\n", calibrated_ ? "loaded from NVS" : "missing");
  if (calibrated_) dumpCalibration();
}

void IndoorPositioning::referencePoint(uint8_t index, float &xM, float &yM) const {
  if (calibrated_ && index < cal_.pointCount) {
    xM = cal_.points[index].xM;
    yM = cal_.points[index].yM;
  } else {
    trianglePoint(kCalibrationSideM, index, xM, yM);
  }
}

void IndoorPositioning::startCalibration() {
  progress_ = CalibrationProgress{};
  progress_.active = true;
  pending_ = Calibration{};
  pending_.sideM = kCalibrationSideM;
  pending_.pointCount = kMaxReferencePoints;
  Serial.printf("IPS: calibration started, side %.2f m, %d scans per point\n", kCalibrationSideM, kScansPerPoint);
}

void IndoorPositioning::confirmCalibrationPoint() {
  if (!progress_.active || progress_.measuring) return;
  float x = 0, y = 0;
  trianglePoint(pending_.sideM, progress_.point, x, y);
  builder_.begin(x, y);
  progress_.measuring = true;
  progress_.scansDone = 0;
  progress_.lastPointFailed = false;
  measureFromMs_ = millis();
  if (!scanner_.busy()) scanner_.start();
  Serial.printf("IPS: measuring point %c\n", 'A' + progress_.point);
}

void IndoorPositioning::cancelCalibration() {
  if (!progress_.active) return;
  progress_ = CalibrationProgress{};
  Serial.println("IPS: calibration cancelled, previous calibration kept");
}

void IndoorPositioning::resetCalibration() {
  eraseCalibration();
  calibrated_ = false;
  cal_ = Calibration{};
  filter_.reset();
  position_ = Position{};
  Serial.println("IPS: calibration deleted");
}

void IndoorPositioning::startPositioning() {
  if (positioning_) return;
  positioning_ = true;
  filter_.reset();
  position_ = Position{};
  nextScanMs_ = millis();
}

void IndoorPositioning::stopPositioning() { positioning_ = false; }

void IndoorPositioning::loop(uint32_t now) {
  switch (scanner_.poll(scan_)) {
  case WifiScanner::State::Done: onScan(scan_); break;
  case WifiScanner::State::Failed:
    Serial.println("IPS: scan failed");
    if (progress_.measuring) scanner_.start(); // Retry; the point is not abandoned.
    break;
  default: break;
  }
  if (scanner_.busy()) return;
  // Calibration scans run back to back; positioning scans every interval.
  if (progress_.measuring) {
    scanner_.start();
  } else if (positioning_ && calibrated_ && !progress_.active && int32_t(now - nextScanMs_) >= 0) {
    nextScanMs_ = now + kScanIntervalMs;
    if (!scanner_.start()) Serial.println("IPS: scan could not start");
  }
}

void IndoorPositioning::onScan(const ScanResult &scan) {
#if IPS_DEBUG
  Serial.printf("IPS scan: %u APs in %lu ms\n", scan.count, (unsigned long)scan.durationMs);
  for (uint16_t i = 0; i < scan.count; ++i) {
    Serial.print("  ");
    printBssid(scan.aps[i].bssid);
    Serial.printf(" %4d dBm ch%2u %s\n", scan.aps[i].rssi, scan.aps[i].channel, scan.aps[i].ssid);
  }
#endif
  if (progress_.measuring) {
    // A scan already running when the user confirmed was taken on the way.
    if (int32_t(scan.startMs - measureFromMs_) < 0) return;
    builder_.addScan(scan);
    progress_.scansDone = uint8_t(builder_.scans());
    if (builder_.scans() >= kScansPerPoint) finishPoint();
    return;
  }
  if (positioning_ && calibrated_ && !progress_.active) evaluate(scan);
}

void IndoorPositioning::finishPoint() {
  Fingerprint &fp = pending_.points[progress_.point];
  const std::size_t kept = builder_.build(fp);
  progress_.measuring = false;
  Serial.printf("IPS: point %c: %u usable APs\n", 'A' + progress_.point, unsigned(kept));
  if (kept < kMinApCount) {
    progress_.lastPointFailed = true; // Stay on this point; the user can retry.
    return;
  }
  if (++progress_.point < pending_.pointCount) return;
  // All points measured: store and switch to positioning.
  cal_ = pending_;
  calibrated_ = true;
  progress_ = CalibrationProgress{};
  if (!saveCalibration(cal_)) Serial.println("IPS: saving calibration to NVS failed");
  else Serial.println("IPS: calibration saved to NVS");
  dumpCalibration();
  positioning_ = false;
  startPositioning();
}

void IndoorPositioning::evaluate(const ScanResult &scan) {
  const uint32_t t0 = micros();
  const PositionEstimate e = locate(cal_.points, cal_.pointCount, scan);
  bool accepted = false;
  if (e.status != PositionStatus::NoPosition) accepted = filter_.update(e.xM, e.yM, e.confidence);
  const uint32_t computeUs = micros() - t0;

  position_.valid = filter_.valid();
  position_.xM = filter_.x();
  position_.yM = filter_.y();
  position_.rawXM = e.xM;
  position_.rawYM = e.yM;
  position_.confidence = e.confidence;
  position_.apCount = e.apCount;
  position_.status = e.status;
  position_.updatedMs = millis();
  position_.scanMs = scan.durationMs;

#if IPS_DEBUG
  for (uint8_t r = 0; r < cal_.pointCount; ++r)
    Serial.printf("IPS  %c: distance %.1f dB, weight %.2f\n", 'A' + r, e.distanceDb[r], e.weight[r]);
  Serial.printf("IPS fix raw %.2f,%.2f -> filtered %.2f,%.2f%s conf %u%% APs %u %s | scan %lu ms, calc %lu us\n",
                e.xM, e.yM, filter_.x(), filter_.y(), accepted ? "" : " (jump held)", e.confidence,
                e.apCount, statusName(e.status), (unsigned long)scan.durationMs, (unsigned long)computeUs);
#else
  (void)accepted;
  (void)computeUs;
#endif
}

void IndoorPositioning::dumpCalibration() const {
#if IPS_DEBUG
  for (uint8_t p = 0; p < cal_.pointCount; ++p) {
    const Fingerprint &f = cal_.points[p];
    Serial.printf("IPS fingerprint %c (%.2f, %.2f): %u APs\n", 'A' + p, f.xM, f.yM, f.count);
    for (uint8_t i = 0; i < f.count; ++i) {
      const ApProfile &a = f.aps[i];
      Serial.print("  ");
      printBssid(a.bssid);
      Serial.printf(" %6.1f dBm sd %4.1f seen %2u/%2u ch%2u %s\n", a.meanRssi, a.stdRssi,
                    a.detections, a.scans, a.channel, a.ssid);
    }
  }
#endif
}

} // namespace ips
