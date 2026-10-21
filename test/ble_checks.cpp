// Host tests for BLE distance logic (no ESP32 needed):
//   g++ -std=c++17 -Isrc/ble_ranging test/ble_checks.cpp src/ble_ranging/BleRanging.cpp -o ble_checks && ./ble_checks
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include "BleRanging.h"

using namespace ble;

namespace {

int failures = 0;
#define CHECK(cond, ...)                                            \
  do {                                                              \
    if (!(cond)) {                                                  \
      ++failures;                                                   \
      std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
      std::printf(__VA_ARGS__);                                     \
      std::printf("\n");                                            \
    }                                                               \
  } while (0)

Address addr(const char *text) {
  Address a;
  Address::parse(text, a);
  return a;
}

Advert advert(const Address &a, int rssi, uint32_t ms, bool apple = false) {
  Advert v;
  v.addr = a;
  v.rssi = int8_t(rssi);
  v.ms = ms;
  v.apple = apple;
  return v;
}

void testAddress() {
  Address a;
  CHECK(Address::parse("5A:23:BE:FC:D0:B1", a), "parse");
  CHECK(a.b[0] == 0x5A && a.b[5] == 0xB1, "byte order as displayed");
  char text[18];
  a.format(text, sizeof text);
  CHECK(std::strcmp(text, "5A:23:BE:FC:D0:B1") == 0, "round trip: %s", text);
  CHECK(Address::parse("5a:23:be:fc:d0:b1", a), "lower case accepted");
  CHECK(!Address::parse("5A:23:BE:FC:D0", a), "too short rejected");
  CHECK(!Address::parse("5A:23:BE:FC:D0:B1:00", a), "too long rejected");
  CHECK(addr("5A:23:BE:FC:D0:B1").isResolvablePrivate(), "0x5A.. is a resolvable private address");
  CHECK(!addr("C4:23:BE:FC:D0:B1").isResolvablePrivate(), "static random is not RPA");
  CHECK(!addr("24:0A:C4:00:00:01").isResolvablePrivate(), "public-looking is not RPA");
  CHECK(Address{}.empty() && !a.empty(), "empty");
}

void testDistanceModel() {
  CHECK(std::fabs(distanceM(-59, -59) - 1.0f) < 1e-4f, "RSSI at 1 m gives 1 m");
  // Matches the phone app in the screenshot: -48 dBm -> 0.28 m with -59 @ 1 m, n = 2.
  CHECK(std::fabs(distanceM(-48, -59) - 0.282f) < 0.005f, "-48 dBm: %.3f m", distanceM(-48, -59));
  CHECK(std::fabs(distanceM(-79, -59) - 10.0f) < 0.01f, "-20 dB is 10x at n=2");
  CHECK(std::fabs(distanceM(-89, -59, 3.0f) - 10.0f) < 0.01f, "-30 dB is 10x at n=3");
  CHECK(std::isnan(distanceM(-60, -59, 0)), "invalid exponent gives NaN");
}

void testRssiFilter() {
  RssiFilter f;
  CHECK(!f.valid(1000) && f.median(1000) == -128, "empty filter has no level");
  const int8_t seq[] = {-60, -61, -59, -60, -30, -62, -60}; // One reflection spike.
  for (int i = 0; i < 7; ++i) f.add(seq[i], 1000 + i * 300);
  CHECK(std::fabs(f.median(3000) + 60) < 0.6f, "median ignores the spike: %.1f", f.median(3000));
  CHECK(f.median(1000 + 6 * 300 + kRssiWindowMs + 1) == -128, "old samples expire");
  // Sample stamped slightly after "now" (another task) still counts.
  RssiFilter g;
  g.add(-50, 5005);
  CHECK(g.median(5000) == -50, "sample newer than now is age 0, not ancient");
}

void testTrackerTarget() {
  Tracker t;
  const Address target = addr("5A:23:BE:FC:D0:B1"), other = addr("11:22:33:44:55:66");
  t.setTarget(target);
  CHECK(!t.seen() && std::isnan(t.distance(1000)), "nothing heard yet");
  for (int i = 0; i < 10; ++i) {
    t.add(advert(target, -48, 1000 + i * 276, true));
    t.add(advert(other, -40, 1010 + i * 300));
  }
  const uint32_t now = 1000 + 9 * 276;
  CHECK(t.fresh(now) && t.targetApple(), "target heard, Apple flag");
  CHECK(std::fabs(t.distance(now) - 0.282f) < 0.01f, "distance from target only: %.3f", t.distance(now));
  CHECK(t.advertIntervalMs() >= 270 && t.advertIntervalMs() <= 282, "interval ~276 ms: %lu",
        (unsigned long)t.advertIntervalMs());
  CHECK(!t.fresh(now + kFreshMs + 1), "goes stale");
  CHECK(std::isnan(t.distance(now + kRssiWindowMs + 1)), "no distance without recent RSSI");
}

void testIntervalIgnoresGaps() {
  Tracker t;
  const Address target = addr("5A:23:BE:FC:D0:B1");
  t.setTarget(target);
  for (int i = 0; i < 5; ++i) t.add(advert(target, -60, 1000 + i * 100));
  t.add(advert(target, -60, 20000)); // Long loss.
  CHECK(t.advertIntervalMs() <= 110, "a long gap is not an advert period: %lu", (unsigned long)t.advertIntervalMs());
}

void testStrongestAndAdopt() {
  Tracker t;
  t.setTarget(addr("5A:23:BE:FC:D0:B1"));
  t.add(advert(addr("AA:00:00:00:00:01"), -70, 1000));
  t.add(advert(addr("AA:00:00:00:00:02"), -45, 1000, true));
  t.add(advert(addr("AA:00:00:00:00:03"), -80, 1000));
  t.add(advert(addr("5A:23:BE:FC:D0:B1"), -30, 1000)); // Target is not a candidate.
  Candidate c;
  CHECK(t.strongest(1500, c) && c.addr == addr("AA:00:00:00:00:02") && c.apple, "strongest other device");
  CHECK(t.candidateCount(1500) == 3, "three others nearby");
  CHECK(!t.strongest(1000 + kFreshMs + 1, c), "stale devices are not offered");
  // Adopting: target switches and its history starts fresh.
  t.setTarget(addr("AA:00:00:00:00:02"));
  CHECK(!t.seen(), "new target starts unseen");
  t.add(advert(addr("AA:00:00:00:00:02"), -59, 2000));
  CHECK(std::fabs(t.distance(2000) - 1.0f) < 1e-3f, "new target ranged");
}

void testCandidateTableFull() {
  Tracker t;
  for (int i = 0; i < int(kMaxCandidates) + 10; ++i) {
    Address a;
    a.b[0] = 0xCC;
    a.b[5] = uint8_t(i);
    t.add(advert(a, -90 + (i % 5), 1000 + i));
  }
  Address strongest;
  strongest.b[0] = 0xDD;
  t.add(advert(strongest, -20, 2000));
  Candidate c;
  CHECK(t.strongest(2000, c) && c.addr == strongest, "new device still enters a full table");
  CHECK(t.candidateCount(2000) <= kMaxCandidates, "table bounded");
}

void testCalibrationShift() {
  Tracker t;
  const Address target = addr("5A:23:BE:FC:D0:B1");
  t.setTarget(target);
  for (int i = 0; i < 8; ++i) t.add(advert(target, -65, 1000 + i * 200));
  t.setRssiAt1m(-65); // "Calibrate at 1 m" with the device held at 1 m.
  CHECK(std::fabs(t.distance(2400) - 1.0f) < 1e-3f, "calibrated reading is 1 m");
}

void testNoisyStationary() {
  // Typical BLE: 4 dB jitter plus 10% deep fades (a body in the way). The
  // 3 s median keeps 95% of estimates within 5 dB (x1.8) of the true 2 m and
  // the typical one within 2 dB (RSSI is whole dB, so ratios move in steps of
  // x1.12); a burst of
  // fades inside one window can still briefly read 2-4x too far, which is a
  // property of the signal, not something a short filter can remove.
  std::mt19937 rng(3);
  std::normal_distribution<float> n(0, 4);
  std::uniform_real_distribution<float> u(0, 1);
  Tracker t;
  const Address target = addr("5A:23:BE:FC:D0:B1");
  t.setTarget(target);
  const float trueRssi = -59 - 20 * std::log10(2.0f);
  float ratios[200];
  int count = 0;
  for (int i = 0; i < 200; ++i) {
    float rssi = trueRssi + n(rng);
    if (u(rng) < 0.1f) rssi -= 15;
    const uint32_t ms = 1000 + i * 280;
    t.add(advert(target, int(std::lround(rssi)), ms));
    if (i > 10) {
      const float ratio = t.distance(ms) / 2.0f;
      ratios[count++] = ratio > 1 ? ratio : 1 / ratio;
    }
  }
  std::sort(ratios, ratios + count);
  const float p95 = ratios[count * 95 / 100];
  CHECK(p95 < 1.8f, "95%% of estimates within x1.8 of 2 m: p95 x%.2f", p95);
  CHECK(ratios[count / 2] < 1.3f, "typical estimate within x1.3: x%.2f", ratios[count / 2]);
}

} // namespace

int main() {
  testAddress();
  testDistanceModel();
  testRssiFilter();
  testTrackerTarget();
  testIntervalIgnoresGaps();
  testStrongestAndAdopt();
  testCandidateTableFull();
  testCalibrationShift();
  testNoisyStationary();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all BLE distance checks passed\n");
  return 0;
}
