#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "MeasFormat.h"
#include "RecorderCore.h"
#include "SampleRing.h"
#include "TriggerDetector.h"
#include "ThresholdUi.h"
#include "UsbOwnership.h"
#include "CardGate.h"

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

struct FakeSink : MeasSink {
  struct Call {
    char kind;  // 'o' open, 'w' write, 'c' close
    int32_t relUs;
    int thresholdDeci;
    bool ok;
    uint32_t at;  // test clock when the call happened
  };
  std::vector<Call> calls;
  bool acceptOpen = true;
  uint32_t now = 0;
  bool open(int thresholdDeci) override {
    calls.push_back({'o', 0, thresholdDeci, acceptOpen, now});
    return acceptOpen;
  }
  void write(int32_t relUs, const Accel &) override { calls.push_back({'w', relUs, 0, true, now}); }
  void close() override { calls.push_back({'c', 0, 0, true, now}); }
  std::size_t count(char kind) const {
    std::size_t n = 0;
    for (const auto &c : calls) n += c.kind == kind;
    return n;
  }
  std::vector<Call> only(char kind) const {
    std::vector<Call> out;
    for (const auto &c : calls)
      if (c.kind == kind) out.push_back(c);
    return out;
  }
};

static void feedCore(RecorderCore &core, FakeSink &sink, uint32_t tStartUs, float seconds,
                     const std::function<Accel(float)> &gen) {
  const uint32_t n = uint32_t(seconds * 500.0f + 0.5f);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t t = tStartUs + i * 2000;
    sink.now = t;
    core.onSample(t, gen(float(t) * 1e-6f), sink);
  }
}

static Accel impulse(float t) {  // 10 Hz, 5 m/s2 on X from t=3 s for 2 s
  if (t < 3.0f || t >= 5.0f) return gravity(t);
  return Accel{5.0f * std::sin(2.0f * 3.14159265f * 10.0f * (t - 3.0f)), 0, 9.81f};
}

static void coreChecks() {
  using M = RecorderCore::Mode;
  {  // silence
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 5.0f, gravity);
    CHECK(sink.calls.empty());
    CHECK(core.mode() == M::Armed);
  }
  {  // one impulse -> one measurement with 1 s pre-roll
    RecorderCore core;
    FakeSink sink;
    CHECK(core.thresholdDeci() == 20);
    feedCore(core, sink, 0, 9.0f, impulse);
    const auto opens = sink.only('o');
    CHECK(opens.size() == 1 && opens[0].thresholdDeci == 20);
    const auto writes = sink.only('w');
    CHECK(!writes.empty());
    if (!writes.empty()) CHECK(writes[0].relUs >= -1000000 && writes[0].relUs <= -998000);
    std::size_t zeros = 0;
    bool step2000 = true;
    for (std::size_t i = 0; i < writes.size(); ++i) {
      zeros += writes[i].relUs == 0;
      if (i && writes[i].relUs - writes[i - 1].relUs != 2000) step2000 = false;
    }
    CHECK(zeros == 1);
    CHECK(step2000);
    const auto closes = sink.only('c');
    CHECK(closes.size() == 1);
    // Stop fires 3 s after the last above-threshold sample, which is a few ms before the
    // impulse ends at 5.0 s (same as in Task 2), so the lower bound is 7.98 s, not 8.0 s.
    if (closes.size() == 1) CHECK(closes[0].at >= 7980000 && closes[0].at <= 8100000);
    CHECK(!sink.calls.empty() && sink.calls.back().kind == 'c');  // nothing written after close
    CHECK(core.mode() == M::Armed);
  }
  {  // refused open -> holdoff between attempts
    RecorderCore core;
    FakeSink sink;
    sink.acceptOpen = false;
    feedCore(core, sink, 0, 3.0f, burstFromZero);
    const auto refused = sink.only('o');
    CHECK(!refused.empty() && refused.size() <= 3);
    for (std::size_t i = 1; i < refused.size(); ++i) CHECK(refused[i].at - refused[i - 1].at >= 1000000);
    CHECK(sink.count('w') == 0 && sink.count('c') == 0);
    CHECK(core.mode() == M::Armed);
    sink.acceptOpen = true;
    const std::size_t before = sink.calls.size();
    feedCore(core, sink, 3000000, 1.5f, burstFromZero);
    std::size_t nextOpen = before;
    while (nextOpen < sink.calls.size() && sink.calls[nextOpen].kind != 'o') ++nextOpen;
    CHECK(nextOpen < sink.calls.size());
    if (nextOpen < sink.calls.size() && !refused.empty()) {
      const auto &o = sink.calls[nextOpen];
      CHECK(o.ok && o.at - refused.back().at >= 1000000 && o.at - refused.back().at <= 1100000);
    }
    CHECK(core.mode() == M::Recording && sink.count('w') > 0);
  }
  {  // USB request during a measurement
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 1.5f, burstFromZero);
    CHECK(core.mode() == M::Recording);
    core.requestUsb(true);
    core.service(sink);
    CHECK(sink.count('c') == 1);
    CHECK(core.mode() == M::Usb);
    const std::size_t inUsb = sink.calls.size();
    feedCore(core, sink, 1500000, 2.0f, burstFromZero);
    core.requestUsb(true);  // repeated request while already in USB mode
    core.service(sink);
    core.onGap(sink);  // gap in USB mode keeps USB and touches no sink
    CHECK(core.mode() == M::Usb);
    CHECK(sink.calls.size() == inUsb);
    core.requestUsb(false);
    core.service(sink);
    CHECK(core.mode() == M::Armed);
    feedCore(core, sink, 3500000, 0.99f, burstFromZero);  // warm-up again
    CHECK(sink.calls.size() == inUsb);
    feedCore(core, sink, 4490000, 0.2f, burstFromZero);
    CHECK(sink.count('o') == 2 && core.mode() == M::Recording);
  }
  {  // gap: close, and the next measurement holds no pre-gap samples
    TriggerDetector::Config cfg;
    cfg.warmupUs = 100000;  // shorter than the pre-roll so stale ring entries would be in its window
    RecorderCore core(cfg);
    FakeSink sink;
    feedCore(core, sink, 0, 1.0f, burstFromZero);
    CHECK(core.mode() == M::Recording);
    core.onGap(sink);
    CHECK(sink.count('c') == 1);
    CHECK(core.mode() == M::Armed);
    const std::size_t afterGap = sink.calls.size();
    const uint32_t resumeUs = 1200000;
    feedCore(core, sink, resumeUs, 1.0f, burstFromZero);
    std::size_t o = afterGap;
    while (o < sink.calls.size() && sink.calls[o].kind != 'o') ++o;
    CHECK(o < sink.calls.size());
    if (o < sink.calls.size()) {
      const uint32_t trig = sink.calls[o].at;
      bool firstSeen = false, ok = true;
      for (std::size_t i = o + 1; i < sink.calls.size(); ++i) {
        const auto &c = sink.calls[i];
        if (c.kind != 'w') continue;
        if (c.relUs < -1000000) ok = false;
        if (!firstSeen) {
          firstSeen = true;
          CHECK(int64_t(trig) + c.relUs >= int64_t(resumeUs));
        }
      }
      CHECK(firstSeen && ok);
    }
  }
  {  // raising the threshold mid-measurement ends it 3 s later
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 2.0f, burstFromZero);
    CHECK(core.mode() == M::Recording);
    core.setThresholdDeci(400);
    CHECK(core.thresholdDeci() == 400);
    feedCore(core, sink, 2000000, 4.0f, burstFromZero);
    const auto closes = sink.only('c');
    CHECK(closes.size() == 1);
    // The quiet run may already have begun at a sub-threshold sample near the last sine
    // zero-crossing before 2.0 s (it does at 1.994 s), so Stop lands at ~4.994 s, not 5.0 s.
    if (closes.size() == 1) CHECK(closes[0].at >= 4980000 && closes[0].at <= 5010000);
    CHECK(core.mode() == M::Armed);
  }
}

void uiChecks() {
  using namespace thrui;
  CHECK(stepDeci(20, +1) == 21);
  CHECK(stepDeci(2, -1) == 2);
  CHECK(stepDeci(400, +1) == 400);
  CHECK(stepDeci(50, +1) == 55);
  CHECK(stepDeci(55, -1) == 50);
  CHECK(stepDeci(50, -1) == 49);
  CHECK(stepDeci(200, +1) == 210);
  CHECK(stepDeci(210, -1) == 200);
  CHECK(stepDeci(200, -1) == 195);
  CHECK(chartRangeMs2(20) == 5.0f);
  CHECK(chartRangeMs2(25) == 5.0f);
  CHECK(chartRangeMs2(26) == 10.0f);
  CHECK(chartRangeMs2(400) == 80.0f);
  CHECK(hit(10, 210) == Button::Minus);
  CHECK(hit(300, 210) == Button::Plus);
  CHECK(hit(160, 210) == Button::None);
  CHECK(hit(10, 100) == Button::None);
  {
    HoldRepeater h;
    CHECK(h.update(Button::Minus, 0) == -1);
    CHECK(h.update(Button::Minus, 100) == 0);
    CHECK(h.update(Button::Minus, 499) == 0);
    CHECK(h.update(Button::Minus, 500) == -1);
    CHECK(h.update(Button::Minus, 550) == 0);
    CHECK(h.update(Button::Minus, 600) == -1);
    CHECK(h.update(Button::None, 610) == 0);
    CHECK(h.update(Button::Minus, 620) == -1);
    CHECK(h.update(Button::Plus, 700) == +1);
    CHECK(h.update(Button::Plus, 1200) == +1);
  }
  {
    UsbOwnership u;
    CHECK(!u.hostOwns());
    u.onAccess();  // access without enumeration must not claim the card
    CHECK(!u.hostOwns());
    u.setEnumerated(true);
    CHECK(u.hostOwns());
    u.onEject();
    CHECK(!u.hostOwns());
    u.onAccess();
    CHECK(u.hostOwns());
    u.onEject();
    CHECK(!u.hostOwns());
    u.setEnumerated(false);
    CHECK(!u.hostOwns());
    u.setEnumerated(true);
    CHECK(u.hostOwns());
    u.setEnumerated(false);
    u.onAccess();
    CHECK(!u.hostOwns());
  }
}

static void gateChecks() {
  CardGate g;
  CHECK(g.enter());      // Granted by default.
  CHECK(!g.release());   // Transfer in flight: not yet.
  CHECK(!g.release());
  g.leave();
  CHECK(g.release());    // Done: card may be taken back...
  CHECK(!g.enter());     // ...and later transfers are refused,
  CHECK(g.release());    // without leaving a transfer marked in flight.
  g.grant();
  CHECK(g.enter());
  g.leave();
  CHECK(g.release());
}

int main() {
  formatChecks();
  ringChecks();
  triggerChecks();
  coreChecks();
  uiChecks();
  gateChecks();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all ok");
  return 0;
}
