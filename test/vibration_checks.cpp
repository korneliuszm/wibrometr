#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "MeasFormat.h"
#include "SampleRing.h"
#include "TriggerDetector.h"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      ++failures;                                                      \
    }                                                                  \
  } while (0)

static std::string sample(int32_t relUs, float ax, float ay, float az) {
  char buf[96];
  const std::size_t n = measfmt::formatSample(buf, sizeof buf, relUs, ax, ay, az);
  return std::string(buf, n);
}

static bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

static void formatChecks() {
  CHECK(sample(-1000000, 0.5f, -1.25f, 9.8066f) == "-1000.000,0.500,-1.250,9.807\n");
  CHECK(sample(2000, 0, 0, 0) == "2.000,0.000,0.000,0.000\n");
  CHECK(sample(-2, 0, 0, 0) == "-0.002,0.000,0.000,0.000\n");
  CHECK(sample(0, 1, 2, 3).rfind("0.000,", 0) == 0);

  char path[32];
  CHECK(measfmt::measPath(7, path, sizeof path) == 15);
  CHECK(std::string(path) == "/MEAS/M0007.CSV");

  unsigned n = 0;
  CHECK(measfmt::parseMeasNumber("/MEAS/m0042.csv", n) && n == 42);
  CHECK(measfmt::parseMeasNumber("M0007.CSV", n) && n == 7);
  CHECK(!measfmt::parseMeasNumber("S0007.CSV", n));
  CHECK(!measfmt::parseMeasNumber("M007.CSV", n));
  CHECK(!measfmt::parseMeasNumber("M0007.TXT", n));

  CHECK(measfmt::nextMeasNumber(0) == 1);
  CHECK(measfmt::nextMeasNumber(41) == 42);
  CHECK(measfmt::nextMeasNumber(9998) == 9999);
  CHECK(measfmt::nextMeasNumber(9999) == 0);

  char buf[512];
  const measfmt::Header h{7, 20, 500.f, 8.f, 1.f, 3.f};
  const std::string header(buf, measfmt::formatHeader(buf, sizeof buf, h));
  bool allComments = !header.empty();
  for (std::size_t i = 0; i < header.size(); ++i)
    if ((i == 0 || header[i - 1] == '\n') && header[i] != '#') allComments = false;
  CHECK(allComments);
  CHECK(contains(header, "# measurement: 7\n"));
  CHECK(contains(header, "# threshold_m_s2: 2.0\n"));
  CHECK(contains(header, "# sample_rate_hz: 500.0\n"));
  CHECK(contains(header, "# range_g: 8\n"));
  CHECK(contains(header, "# pre_trigger_s: 1.0\n"));
  CHECK(contains(header, "# quiet_s: 3.0\n"));
  CHECK(contains(header, "# trigger_at: t_ms = 0\n"));
  const char *columns = "# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2\n";
  CHECK(header.size() >= std::strlen(columns) &&
        header.compare(header.size() - std::strlen(columns), std::string::npos, columns) == 0);

  const std::string footer(buf, measfmt::formatFooter(buf, sizeof buf, 1234, 5));
  CHECK(footer == "# end: samples=1234 dropped=5\n");
}

static void ringChecks() {
  SampleRing<4> r;
  CHECK(r.size() == 0);
  for (uint32_t i = 1; i <= 6; ++i) r.push(i * 1000, Accel{float(i), 0, 0});
  CHECK(r.size() == 4);
  CHECK(r.at(0).tUs == 3000 && r.at(0).a.x == 3.0f);
  CHECK(r.at(3).tUs == 6000 && r.at(3).a.x == 6.0f);
  r.clear();
  CHECK(r.size() == 0);
}

using TimedEvent = std::pair<uint32_t, TriggerDetector::Event>;

static std::vector<TimedEvent> feed(TriggerDetector &d, uint32_t tStartUs, float seconds,
                                    const std::function<Accel(float)> &gen, float thresholdMs2) {
  std::vector<TimedEvent> out;
  const uint32_t n = uint32_t(seconds * 500.0f + 0.5f);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t t = tStartUs + i * 2000;
    const auto e = d.update(t, gen(float(t) * 1e-6f), thresholdMs2);
    if (e != TriggerDetector::Event::None) out.push_back({t, e});
  }
  return out;
}

static Accel gravity(float) { return Accel{0, 0, 9.81f}; }
static Accel burst(float t) {  // 10 Hz, 5 m/s2 on X from t=3 s for 5 s
  if (t < 3.0f || t >= 8.0f) return gravity(t);
  return Accel{5.0f * std::sin(2.0f * 3.14159265f * 10.0f * (t - 3.0f)), 0, 9.81f};
}
static Accel burstFromZero(float t) { return Accel{5.0f * std::sin(2.0f * 3.14159265f * 10.0f * t), 0, 9.81f}; }

static void triggerChecks() {
  using E = TriggerDetector::Event;
  {
    TriggerDetector d;
    CHECK(feed(d, 0, 5.0f, gravity, 2.0f).empty());
  }
  {
    TriggerDetector d;
    const auto tilt = [](float t) {
      const float ang = 1.5707963f * t / 30.0f;
      return Accel{9.81f * std::sin(ang), 0, 9.81f * std::cos(ang)};
    };
    CHECK(feed(d, 0, 30.0f, tilt, 2.0f).empty());
  }
  {
    TriggerDetector d;
    const auto ev = feed(d, 0, 14.0f, burst, 2.0f);
    CHECK(ev.size() == 2);
    if (ev.size() == 2) {
      CHECK(ev[0].second == E::Start && ev[0].first >= 3000000 && ev[0].first <= 3100000);
      // last above-threshold sample is ~7.992 s, so Stop lands at ~10.994 s
      CHECK(ev[1].second == E::Stop && ev[1].first >= 10990000 && ev[1].first <= 11100000);
    }
  }
  {
    TriggerDetector d;
    const auto ev = feed(d, 0, 3.0f, burstFromZero, 2.0f);
    CHECK(!ev.empty());
    if (!ev.empty()) CHECK(ev[0].second == E::Start && ev[0].first >= 1000000 && ev[0].first <= 1100000);
  }
  {
    TriggerDetector d;
    feed(d, 0, 2.0f, burstFromZero, 2.0f);
    CHECK(d.recording());
    d.reset();
    CHECK(!d.recording());
    // first sample after reset() seeds the high-pass filter: no dynamic part yet
    d.update(2000000, burstFromZero(2.0f), 2.0f);
    const Accel dyn = d.dynamic();
    CHECK(std::fabs(dyn.x) < 1e-6f && std::fabs(dyn.y) < 1e-6f && std::fabs(dyn.z) < 1e-6f);
    CHECK(feed(d, 2002000, 0.98f, burstFromZero, 2.0f).empty());
  }
  {
    TriggerDetector d;
    feed(d, 0, 2.0f, burstFromZero, 2.0f);
    CHECK(d.recording());
    d.cancelRecording();
    CHECK(!d.recording());
    const auto ev = feed(d, 2000000, 0.1f, burstFromZero, 2.0f);
    CHECK(!ev.empty() && ev[0].second == E::Start);
  }
  {
    // Warm-up must survive the uint32 microsecond clock wrapping (~71.6 min).
    TriggerDetector d;
    const uint32_t t0 = 0xFFFFF000u;
    const auto wave = [](uint32_t i) {
      return Accel{5.0f * std::sin(2.0f * 3.14159265f * 10.0f * float(i) * 0.002f), 0, 9.81f};
    };
    std::vector<TimedEvent> ev;
    for (uint32_t i = 0; i < 1000; ++i) {  // 2 s burst spanning the wrap
      const uint32_t t = t0 + i * 2000;
      const auto e = d.update(t, wave(i), 2.0f);
      if (e != E::None) ev.push_back({t, e});
    }
    CHECK(ev.size() == 1);
    if (!ev.empty()) {
      const uint32_t since = ev[0].first - t0;
      CHECK(ev[0].second == E::Start && since >= 1000000 && since <= 1100000);
    }
    d.cancelRecording();
    // Quiet samples once a second until a full 2^32 us have elapsed since the first sample.
    uint32_t t = t0 + 2000000;
    for (uint32_t k = 0; k < 4292; ++k, t += 1000000) CHECK(d.update(t, gravity(0), 2.0f) == E::None);
    t = t0 + 100000;  // == t0 + 2^32 + 0.1 s: wrapped elapsed time looks like 0.1 s
    ev.clear();
    for (uint32_t i = 0; i < 100; ++i) {
      const uint32_t ti = t + i * 2000;
      const auto e = d.update(ti, wave(i), 2.0f);
      if (e != E::None) ev.push_back({ti, e});
    }
    CHECK(!ev.empty() && ev[0].second == E::Start && uint32_t(ev[0].first - t) <= 20000);
  }
}

int main() {
  formatChecks();
  ringChecks();
  triggerChecks();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all ok");
  return 0;
}
