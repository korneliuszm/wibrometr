// Host tests for the indoor positioning algorithm (no ESP32 needed):
//   g++ -std=c++17 -Isrc/indoor_positioning test/ips_checks.cpp
//       src/indoor_positioning/WifiFingerprint.cpp
//       src/indoor_positioning/FingerprintLocator.cpp
//       src/indoor_positioning/PositionFilter.cpp -o ips_checks && ./ips_checks
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include "FingerprintLocator.h"
#include "PositionFilter.h"
#include "WifiFingerprint.h"

using namespace ips;

namespace {

int failures = 0;
#define CHECK(cond, ...)                                                   \
  do {                                                                     \
    if (!(cond)) {                                                         \
      ++failures;                                                          \
      std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);        \
      std::printf(__VA_ARGS__);                                            \
      std::printf("\n");                                                   \
    }                                                                      \
  } while (0)

constexpr float kSide = 5.0f;
const float kRefX[3] = {0, kSide, kSide / 2};
const float kRefY[3] = {0, 0, kSide * 0.8660254f};

// Synthetic room: APs at fixed places, log-distance path loss plus a fixed
// per-AP wall/offset term. Only used to make plausible test data; the
// algorithm itself assumes no path-loss model.
struct Room {
  static constexpr int kAps = 8;
  float apX[kAps] = {-3, 9, 2.5f, -2, 8, 2, 12, -6};
  float apY[kAps] = {-2, -1, 9, 5, 6, -6, 3, 8};
  float offset[kAps] = {0, -4, -2, -6, 0, -3, -8, -5};
  float rssiAt(int ap, float x, float y) const {
    const float d = std::fmax(0.5f, std::hypot(x - apX[ap], y - apY[ap]));
    return -38.0f + offset[ap] - 25.0f * std::log10(d);
  }
};

Bssid bssidOf(int i) {
  Bssid b;
  const uint8_t raw[6] = {0x24, 0x0A, 0xC4, 0x10, 0x20, uint8_t(i)};
  std::memcpy(b.b, raw, 6);
  return b;
}

ApReading reading(int i, float rssi) {
  ApReading r;
  r.bssid = bssidOf(i);
  r.rssi = int8_t(std::lround(std::fmax(-127.0f, std::fmin(0.0f, rssi))));
  r.channel = uint8_t(1 + i % 11);
  std::snprintf(r.ssid, sizeof r.ssid, "net%d", i);
  return r;
}

struct Sim {
  Room room;
  std::mt19937 rng{1234};
  float noiseDb = 2.0f;
  float dropout = 0.1f;        // Chance an AP is missing from one scan.
  bool visible[Room::kAps];
  Sim() { for (bool &v : visible) v = true; }

  ScanResult scanAt(float x, float y) {
    // ponytail: stddev must be >0 for std::normal_distribution (noiseDb=0 means "no noise")
    std::normal_distribution<float> noise(0, noiseDb > 0 ? noiseDb : 1e-6f);
    std::uniform_real_distribution<float> u(0, 1);
    ScanResult s;
    for (int i = 0; i < Room::kAps; ++i) {
      if (!visible[i] || u(rng) < dropout) continue;
      const float rssi = room.rssiAt(i, x, y) + noise(rng);
      if (rssi < -95) continue; // Below the radio's sensitivity.
      s.add(reading(i, rssi));
    }
    return s;
  }

  void calibrate(Fingerprint refs[3]) {
    for (int p = 0; p < 3; ++p) {
      FingerprintBuilder b;
      b.begin(kRefX[p], kRefY[p]);
      for (int n = 0; n < kScansPerPoint; ++n) b.addScan(scanAt(kRefX[p], kRefY[p]));
      b.build(refs[p]);
    }
  }
};

float dist(float x, float y, float x2, float y2) { return std::hypot(x - x2, y - y2); }

// ---------------------------------------------------------------- builder

void testRobustStats() {
  const int8_t steady[] = {-60, -61, -59, -60, -60, -61, -59, -30}; // One spike.
  float mean = 0, sd = 0;
  robustStats(steady, 8, mean, sd);
  CHECK(std::fabs(mean + 60) < 0.6f, "outlier must not pull the mean: %.2f", mean);
  CHECK(sd < 1.5f, "outlier must not inflate the spread: %.2f", sd);
}

void testBuilderFilters() {
  FingerprintBuilder b;
  b.begin(0, 0);
  std::mt19937 rng(5);
  std::normal_distribution<float> wild(0, 15);
  for (int n = 0; n < 15; ++n) {
    ScanResult s;
    s.add(reading(0, -55));                       // stable, strong
    s.add(reading(1, -93));                       // too weak
    if (n < 4) s.add(reading(2, -60));            // seen in 4/15 scans
    s.add(reading(3, -70 + wild(rng)));           // unstable
    s.add(reading(4, n == 7 ? -20 : -66));        // stable with one spike
    b.addScan(s);
  }
  Fingerprint f;
  b.build(f);
  CHECK(f.find(bssidOf(0)) != nullptr, "stable AP kept");
  CHECK(f.find(bssidOf(1)) == nullptr, "weak AP dropped");
  CHECK(f.find(bssidOf(2)) == nullptr, "rarely seen AP dropped");
  CHECK(f.find(bssidOf(3)) == nullptr, "unstable AP dropped");
  const ApProfile *spiky = f.find(bssidOf(4));
  CHECK(spiky && std::fabs(spiky->meanRssi + 66) < 0.5f, "spike rejected, mean %.1f",
        spiky ? spiky->meanRssi : 0.0f);
  CHECK(f.count == 2, "two APs kept, got %u", f.count);
  CHECK(f.aps[0].meanRssi >= f.aps[1].meanRssi, "strongest first");
  CHECK(f.aps[0].detections == 15 && f.aps[0].scans == 15, "detections recorded");
  CHECK(std::strcmp(f.aps[0].ssid, "net0") == 0 && f.aps[0].channel == 1, "SSID and channel recorded");
}

// ---------------------------------------------------------------- locator

void testAtReferences() {
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  for (int p = 0; p < 3; ++p) {
    // Average over a few scans: a single scan is noisy by design.
    float sx = 0, sy = 0;
    int okCount = 0;
    for (int n = 0; n < 10; ++n) {
      PositionEstimate e = locate(refs, 3, sim.scanAt(kRefX[p], kRefY[p]));
      sx += e.xM;
      sy += e.yM;
      okCount += e.status == PositionStatus::Ok;
      CHECK(e.status != PositionStatus::NoPosition, "point %c must give a position", 'A' + p);
    }
    const float err = dist(sx / 10, sy / 10, kRefX[p], kRefY[p]);
    CHECK(err < 0.3f * kSide, "point %c: mean fix %.2f,%.2f is %.2f m off", 'A' + p, sx / 10, sy / 10, err);
    CHECK(okCount >= 7, "point %c: confidence should be OK in most scans (%d/10)", 'A' + p, okCount);
  }
}

void testIntermediate() {
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  // Midpoint of A-B, then the centroid.
  const float mx = kSide / 2, my = 0;
  float sx = 0, sy = 0;
  for (int n = 0; n < 10; ++n) {
    PositionEstimate e = locate(refs, 3, sim.scanAt(mx, my));
    sx += e.xM / 10;
    sy += e.yM / 10;
  }
  CHECK(sx > 0.25f * kSide && sx < 0.75f * kSide, "A-B midpoint x between A and B: %.2f", sx);
  CHECK(sy < 0.4f * kRefY[2], "A-B midpoint stays near edge AB: y %.2f", sy);
  const float cx = kSide / 2, cy = kRefY[2] / 3;
  sx = sy = 0;
  for (int n = 0; n < 10; ++n) {
    PositionEstimate e = locate(refs, 3, sim.scanAt(cx, cy));
    sx += e.xM / 10;
    sy += e.yM / 10;
  }
  CHECK(dist(sx, sy, cx, cy) < 0.3f * kSide, "centroid fix %.2f,%.2f vs %.2f,%.2f", sx, sy, cx, cy);
}

void testMissingAps() {
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  sim.visible[1] = sim.visible[4] = sim.visible[6] = false; // Three APs switched off.
  PositionEstimate full{}, part{};
  float sx = 0, sy = 0;
  for (int n = 0; n < 10; ++n) {
    part = locate(refs, 3, sim.scanAt(kRefX[0], kRefY[0]));
    sx += part.xM / 10;
    sy += part.yM / 10;
  }
  CHECK(part.status != PositionStatus::NoPosition, "still locates with 5 of 8 APs");
  CHECK(dist(sx, sy, kRefX[0], kRefY[0]) < 0.35f * kSide, "still near A: %.2f,%.2f", sx, sy);
  sim.visible[1] = sim.visible[4] = sim.visible[6] = true;
  sim.dropout = 0;
  full = locate(refs, 3, sim.scanAt(kRefX[0], kRefY[0]));
  sim.visible[1] = sim.visible[4] = sim.visible[6] = false;
  part = locate(refs, 3, sim.scanAt(kRefX[0], kRefY[0]));
  CHECK(part.apCount < full.apCount, "apCount reflects missing APs: %u vs %u", part.apCount, full.apCount);
  CHECK(part.confidence <= full.confidence, "missing APs must not raise confidence: %u vs %u",
        part.confidence, full.confidence);
}

void testUnknownAps() {
  Sim sim;
  sim.dropout = 0;
  Fingerprint refs[3];
  sim.calibrate(refs);
  sim.noiseDb = 0;
  ScanResult base = sim.scanAt(kRefX[1], kRefY[1]);
  ScanResult extra = base;
  for (int i = 100; i < 110; ++i) extra.add(reading(i, -45)); // Strong, never calibrated.
  PositionEstimate a = locate(refs, 3, base), b = locate(refs, 3, extra);
  CHECK(std::fabs(a.xM - b.xM) < 1e-4f && std::fabs(a.yM - b.yM) < 1e-4f,
        "unknown APs must not move the fix: %.3f,%.3f vs %.3f,%.3f", a.xM, a.yM, b.xM, b.yM);
  CHECK(a.apCount == b.apCount, "unknown APs are not counted");
}

void testLargeFluctuations() {
  // 8 dB noise and 30% dropouts: about as large as the differences between
  // the reference points themselves, so single fixes are near random. The
  // checks are what can honestly be expected: finite fixes inside the
  // calibration area, smoothing that narrows the spread, and an average that
  // still leans towards the true point.
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  sim.noiseDb = 8.0f;
  sim.dropout = 0.3f;
  PositionFilter filter;
  float sx = 0, sy = 0, rawSpread = 0, filtSpread = 0;
  float lastRawX = 0, lastFiltX = 0;
  int n = 0;
  for (int i = 0; i < 80; ++i) {
    PositionEstimate e = locate(refs, 3, sim.scanAt(kRefX[0], kRefY[0]));
    CHECK(std::isfinite(e.xM) && std::isfinite(e.yM), "fix must stay finite");
    if (e.status == PositionStatus::NoPosition) continue;
    CHECK(e.xM >= -1e-3f && e.xM <= kSide + 1e-3f && e.yM >= -1e-3f && e.yM <= kRefY[2] + 1e-3f,
          "fix inside the calibration area: %.2f,%.2f", e.xM, e.yM);
    filter.update(e.xM, e.yM, e.confidence);
    if (i >= 10) {
      sx += filter.x();
      sy += filter.y();
      rawSpread += std::fabs(e.xM - lastRawX);
      filtSpread += std::fabs(filter.x() - lastFiltX);
      ++n;
    }
    lastRawX = e.xM;
    lastFiltX = filter.x();
  }
  sx /= n;
  sy /= n;
  const float dA = dist(sx, sy, kRefX[0], kRefY[0]);
  CHECK(dA < dist(sx, sy, kRefX[1], kRefY[1]) && dA < dist(sx, sy, kRefX[2], kRefY[2]),
        "average filtered fix %.2f,%.2f leans towards A", sx, sy);
  CHECK(filtSpread < rawSpread, "filter calms the point: step sum %.1f vs raw %.1f", filtSpread, rawSpread);
}

void testSingleWildAp() {
  Sim sim;
  sim.dropout = 0;
  sim.noiseDb = 0;
  Fingerprint refs[3];
  sim.calibrate(refs);
  ScanResult s = sim.scanAt(kRefX[0], kRefY[0]);
  PositionEstimate clean = locate(refs, 3, s);
  s.aps[0].rssi = int8_t(s.aps[0].rssi > -60 ? -95 : -30); // One AP goes haywire.
  PositionEstimate hit = locate(refs, 3, s);
  CHECK(dist(clean.xM, clean.yM, hit.xM, hit.yM) < 0.25f * kSide,
        "one wild AP moves the fix only a little: %.2f m", dist(clean.xM, clean.yM, hit.xM, hit.yM));
}

void testInsufficientData() {
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  ScanResult empty;
  PositionEstimate e = locate(refs, 3, empty);
  CHECK(e.status == PositionStatus::NoPosition && e.apCount == 0, "empty scan: no position");
  CHECK(std::isfinite(e.distanceDb[0]), "distances stay finite");
  ScanResult two;
  two.add(reading(0, -50));
  two.add(reading(1, -60));
  e = locate(refs, 3, two);
  CHECK(e.status == PositionStatus::NoPosition, "two APs: no position (status %s)", statusName(e.status));
  Fingerprint none[3];
  e = locate(none, 3, sim.scanAt(0, 0));
  CHECK(e.status == PositionStatus::NoPosition, "empty calibration: no position");
  e = locate(refs, 1, sim.scanAt(0, 0));
  CHECK(e.status == PositionStatus::NoPosition, "one reference: no position");
  e = locate(nullptr, 3, sim.scanAt(0, 0));
  CHECK(e.status == PositionStatus::NoPosition, "no references: no position");
}

void testIdenticalReferences() {
  Sim sim;
  sim.dropout = 0;
  Fingerprint refs[3];
  sim.calibrate(refs);
  refs[1] = refs[0];
  refs[2] = refs[0];
  refs[1].xM = kRefX[1];
  refs[2].xM = kRefX[2];
  refs[2].yM = kRefY[2];
  PositionEstimate e = locate(refs, 3, sim.scanAt(0, 0));
  CHECK(e.confidence < kOkConfidence, "indistinguishable references: low confidence, got %u", e.confidence);
  CHECK(std::isfinite(e.xM), "perfect matches stay finite (no divide by zero)");
}

void testPerfectMatch() {
  // Scan equal to the A profile: distance 0, must not divide by zero.
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  ScanResult s;
  for (uint8_t i = 0; i < refs[0].count; ++i) {
    ApReading r;
    r.bssid = refs[0].aps[i].bssid;
    r.rssi = int8_t(std::lround(refs[0].aps[i].meanRssi));
    s.add(r);
  }
  PositionEstimate e = locate(refs, 3, s);
  CHECK(std::isfinite(e.xM) && std::isfinite(e.yM), "finite at zero distance");
  CHECK(dist(e.xM, e.yM, kRefX[0], kRefY[0]) < 0.2f * kSide, "lands on A: %.2f,%.2f", e.xM, e.yM);
}

// ---------------------------------------------------------------- filter

void testStationary() {
  Sim sim;
  Fingerprint refs[3];
  sim.calibrate(refs);
  PositionFilter filter;
  const float cx = kSide / 2, cy = kRefY[2] / 3;
  float rawMin = 1e9f, rawMax = -1e9f, filtMin = 1e9f, filtMax = -1e9f;
  int moves = 0;
  float lastX = 0, lastY = 0;
  for (int n = 0; n < 60; ++n) {
    PositionEstimate e = locate(refs, 3, sim.scanAt(cx, cy));
    if (e.status == PositionStatus::NoPosition) continue;
    filter.update(e.xM, e.yM, e.confidence);
    if (n < 10) { lastX = filter.x(); lastY = filter.y(); continue; } // Settle.
    rawMin = std::fmin(rawMin, e.xM);
    rawMax = std::fmax(rawMax, e.xM);
    filtMin = std::fmin(filtMin, filter.x());
    filtMax = std::fmax(filtMax, filter.x());
    if (filter.x() != lastX || filter.y() != lastY) ++moves;
    lastX = filter.x();
    lastY = filter.y();
  }
  CHECK(filtMax - filtMin < rawMax - rawMin, "filter narrows the spread: %.2f vs raw %.2f",
        filtMax - filtMin, rawMax - rawMin);
  // Centre of the triangle is the most ambiguous place: matches to A, B and C
  // are similar, so 2 dB noise moves the fix most there.
  CHECK(filtMax - filtMin < 0.3f * kSide, "stationary spread %.2f m", filtMax - filtMin);
  CHECK(moves < 50, "dead zone suppresses jitter: displayed point moved %d times", moves);
}

void testFilterJumps() {
  PositionFilter f;
  f.update(1, 1, 100);
  CHECK(!f.update(4.5f, 1, 100), "single far fix is held back");
  CHECK(std::fabs(f.x() - 1) < 1e-6f, "held-back fix does not move the point");
  f.update(1.1f, 1, 100);
  CHECK(f.x() < 1.2f, "back to normal after a lone outlier");
  // A real move: two consecutive far fixes are accepted.
  CHECK(!f.update(4.5f, 1, 100), "first far fix held");
  CHECK(f.update(4.4f, 1, 100), "second far fix confirms the move");
  CHECK(f.smoothedX() > 3.0f, "point follows a confirmed move decisively: %.2f", f.smoothedX());
}

void testFilterDeadzone() {
  PositionFilter f;
  f.update(2, 2, 100);
  f.update(2.05f, 2, 100);
  CHECK(f.x() == 2.0f, "sub-deadzone change not shown");
  for (int i = 0; i < 10; ++i) f.update(2.6f, 2, 100);
  CHECK(f.x() > 2.4f, "real drift is shown: %.2f", f.x());
}

void testFilterRejectsNan() {
  PositionFilter f;
  CHECK(!f.update(NAN, 1, 50), "NaN rejected");
  CHECK(!f.valid(), "NaN does not initialise the filter");
}

} // namespace

int main() {
  testRobustStats();
  testBuilderFilters();
  testAtReferences();
  testIntermediate();
  testMissingAps();
  testUnknownAps();
  testLargeFluctuations();
  testSingleWildAp();
  testInsufficientData();
  testIdenticalReferences();
  testPerfectMatch();
  testStationary();
  testFilterJumps();
  testFilterDeadzone();
  testFilterRejectsNan();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all indoor positioning checks passed\n");
  return 0;
}
