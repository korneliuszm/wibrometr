#include <cstdio>
#include <cstring>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "CountedClock.h"
#include "FifoClock.h"
#include "MeasFormat.h"
#include "MeasStats.h"
#include "LiveSpectrum.h"
#include "PeakLog.h"
#include "SwipeGuard.h"
#include "TouchCalibration.h"
#include "UsbPageUi.h"
#include "RecorderCore.h"
#include "SampleRing.h"
#include "TriggerDetector.h"
#include <initializer_list>
#include "RangeTrigger.h"
#include "RangeUi.h"
#include "Vl53l0xDecode.h"
#include "UsbOwnership.h"
#include "CardGate.h"
#include "CardHandover.h"

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
  const measfmt::Header h{7, 300, 500.f, 8.f, 1.f, 5.f};
  const std::string header(buf, measfmt::formatHeader(buf, sizeof buf, h));
  // Every line is a '#' comment except the final plain column-names line.
  const char *columnLine = "t_ms,ax_m_s2,ay_m_s2,az_m_s2\n";
  const std::size_t columnLen = std::strlen(columnLine);
  CHECK(header.size() > columnLen && header.compare(header.size() - columnLen, columnLen, columnLine) == 0);
  const std::string comments = header.size() > columnLen ? header.substr(0, header.size() - columnLen) : "";
  bool allComments = !comments.empty();
  for (std::size_t i = 0; i < comments.size(); ++i)
    if ((i == 0 || comments[i - 1] == '\n') && comments[i] != '#') allComments = false;
  CHECK(allComments);
  CHECK(contains(header, "# measurement: 7\n"));
  CHECK(contains(header, "# trigger_distance_mm: 300\n"));
  CHECK(contains(header, "# sample_rate_hz: 500.0\n"));
  CHECK(contains(header, "# range_g: 8\n"));
  CHECK(contains(header, "# pre_trigger_s: 1.0\n"));
  CHECK(contains(header, "# record_s: 5.0\n"));
  CHECK(contains(header, "# trigger_at: t_ms = 0\n"));
  const char *columns = "# columns: t_ms,ax_m_s2,ay_m_s2,az_m_s2\n";
  CHECK(comments.size() >= std::strlen(columns) &&
        comments.compare(comments.size() - std::strlen(columns), std::string::npos, columns) == 0);

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
    int triggerMm;
    bool ok;
    uint32_t at;  // test clock when the call happened
  };
  std::vector<Call> calls;
  bool acceptOpen = true;
  uint32_t now = 0;
  bool open(int triggerMm) override {
    calls.push_back({'o', 0, triggerMm, acceptOpen, now});
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

// requestStart() is taken by the next sample; a feed in two parts lets a test start
// in the middle of a stream.
static void coreChecks() {
  using M = RecorderCore::Mode;
  const auto firstWrite = [](const FakeSink &sink) {
    const auto writes = sink.only('w');
    return writes.empty() ? int32_t(0) : writes[0].relUs;
  };
  {  // silence, and vibration alone does not start anything
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 5.0f, burstFromZero);
    CHECK(sink.calls.empty());
    CHECK(core.mode() == M::Armed);
  }
  {  // one start request -> one measurement: 1 s pre-roll, 5 s after the trigger
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 3.0f, gravity);
    core.requestStart(300);
    CHECK(core.startPending());
    feedCore(core, sink, 3000000, 8.0f, gravity);
    CHECK(!core.startPending());
    const auto opens = sink.only('o');
    CHECK(opens.size() == 1 && opens[0].triggerMm == 300 && opens[0].at == 3000000);
    const auto writes = sink.only('w');
    CHECK(!writes.empty());
    CHECK(firstWrite(sink) >= -1000000 && firstWrite(sink) <= -998000);
    std::size_t zeros = 0;
    bool step2000 = true;
    for (std::size_t i = 0; i < writes.size(); ++i) {
      zeros += writes[i].relUs == 0;
      if (i && writes[i].relUs - writes[i - 1].relUs != 2000) step2000 = false;
    }
    CHECK(zeros == 1);
    CHECK(step2000);
    CHECK(writes.back().relUs >= 4990000 && writes.back().relUs <= 5002000);
    const auto closes = sink.only('c');
    CHECK(closes.size() == 1);
    if (closes.size() == 1) CHECK(closes[0].at >= 7990000 && closes[0].at <= 8004000);
    CHECK(sink.calls.back().kind == 'c');  // nothing written after close
    CHECK(core.mode() == M::Armed);
  }
  {  // settings: shorter pre-roll, other recording time
    RecorderCore core;
    FakeSink sink;
    core.setPreRollUs(300000);
    core.setRecordUs(2000000);
    feedCore(core, sink, 0, 3.0f, gravity);
    core.requestStart(120);
    feedCore(core, sink, 3000000, 4.0f, gravity);
    CHECK(firstWrite(sink) >= -300000 && firstWrite(sink) <= -298000);
    const auto closes = sink.only('c');
    CHECK(closes.size() == 1 && closes[0].at >= 4990000 && closes[0].at <= 5004000);
    core.setPreRollUs(5000000);  // clamped to the 1 s the ring holds
    core.requestStart(120);
    sink.calls.clear();
    feedCore(core, sink, 7000000, 0.5f, gravity);
    CHECK(firstWrite(sink) >= -1000000);
  }
  {  // a start request in the first moments: the pre-roll is whatever the ring holds
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 0.2f, gravity);
    core.requestStart(300);
    feedCore(core, sink, 200000, 0.1f, gravity);
    CHECK(core.mode() == M::Recording && firstWrite(sink) >= -202000);
  }
  {  // a request while recording is dropped, not queued for after the measurement
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 1.5f, gravity);
    core.requestStart(300);
    feedCore(core, sink, 1500000, 1.0f, gravity);
    CHECK(core.mode() == M::Recording);
    core.requestStart(300);
    feedCore(core, sink, 2500000, 6.0f, gravity);
    CHECK(sink.count('o') == 1 && sink.count('c') == 1 && core.mode() == M::Armed && !core.startPending());
  }
  {  // refused open -> no recording, the request is gone, holdoff between attempts
    RecorderCore core;
    FakeSink sink;
    sink.acceptOpen = false;
    feedCore(core, sink, 0, 1.0f, gravity);
    core.requestStart(300);
    feedCore(core, sink, 1000000, 0.1f, gravity);
    CHECK(sink.count('o') == 1 && sink.count('w') == 0 && sink.count('c') == 0);
    CHECK(core.mode() == M::Armed && !core.startPending());
    core.requestStart(300);  // inside the 1 s holdoff
    feedCore(core, sink, 1100000, 0.5f, gravity);
    CHECK(sink.count('o') == 1 && !core.startPending());
    sink.acceptOpen = true;
    core.requestStart(300);
    feedCore(core, sink, 2100000, 0.2f, gravity);
    CHECK(sink.count('o') == 2 && core.mode() == M::Recording);
  }
  {  // USB: requests are ignored, a running measurement is closed, then it arms again
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 1.0f, gravity);
    core.requestStart(300);
    feedCore(core, sink, 1000000, 1.0f, gravity);
    CHECK(core.mode() == M::Recording);
    core.requestUsb(true);
    core.service(sink);
    CHECK(sink.count('c') == 1 && core.mode() == M::Usb);
    const std::size_t inUsb = sink.calls.size();
    core.requestStart(300);
    feedCore(core, sink, 2000000, 1.0f, gravity);
    CHECK(sink.calls.size() == inUsb && !core.startPending());
    core.onGap(sink);  // gap in USB mode keeps USB and touches no sink
    CHECK(core.mode() == M::Usb && sink.calls.size() == inUsb);
    core.requestUsb(false);
    core.service(sink);
    CHECK(core.mode() == M::Armed);
    core.requestStart(300);
    feedCore(core, sink, 3000000, 0.2f, gravity);
    CHECK(sink.count('o') == 2 && core.mode() == M::Recording);
  }
  {  // gap: close, and the next measurement holds no pre-gap samples
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 1.0f, gravity);
    core.requestStart(300);
    feedCore(core, sink, 1000000, 0.5f, gravity);
    CHECK(core.mode() == M::Recording);
    core.onGap(sink);
    CHECK(sink.count('c') == 1 && core.mode() == M::Armed);
    const std::size_t afterGap = sink.calls.size();
    const uint32_t resumeUs = 2000000;
    feedCore(core, sink, resumeUs, 0.5f, gravity);
    core.requestStart(300);
    feedCore(core, sink, resumeUs + 500000, 0.5f, gravity);
    std::size_t o = afterGap;
    while (o < sink.calls.size() && sink.calls[o].kind != 'o') ++o;
    CHECK(o < sink.calls.size());
    if (o < sink.calls.size()) {
      const uint32_t trig = sink.calls[o].at;
      for (std::size_t i = o + 1; i < sink.calls.size(); ++i)
        if (sink.calls[i].kind == 'w') {
          CHECK(int64_t(trig) + sink.calls[i].relUs >= int64_t(resumeUs));
          break;
        }
    }
  }
  {  // the chart value still comes from the high-pass filter
    RecorderCore core;
    FakeSink sink;
    feedCore(core, sink, 0, 2.0f, burstFromZero);
    CHECK(sink.calls.empty() && std::fabs(core.dynamic().x) > 0.1f);
  }
}

void rangeChecks() {
  using E = RangeTrigger::Event;
  constexpr uint16_t kNone = RangeTrigger::kNoReading;
  const auto feed = [](RangeTrigger &t, std::initializer_list<uint16_t> mm, uint16_t trigger = 300) {
    int starts = 0;
    for (const uint16_t v : mm) starts += t.update(v, trigger) == E::Start;
    return starts;
  };
  {  // Three of the last five near and in agreement: an object that stays.
    RangeTrigger t;
    CHECK(feed(t, {500, 500, 500, 500, 500}) == 0);
    CHECK(feed(t, {250, 252}) == 0);
    CHECK(t.update(248, 300) == E::Start && t.fired());
  }
  {  // Scattered wrong readings (near or far) do not start anything, however often they come.
    RangeTrigger a, b, c;
    CHECK(feed(a, {500, 30, 500, 500, 30, 500, 500, 500, 30, 500, 500, 30, 500, 500}) == 0);
    CHECK(feed(b, {30, 30, 500, 500, 500, 30, 30, 500, 500, 500, 30, 30, 500, 500, 500}) == 0);
    CHECK(feed(c, {kNone, 30, kNone, 30, kNone, 500, 500}) == 0);
  }
  {  // Even three near readings in five do not start it when they disagree: a ghost cannot be
     // told from an object by count alone, but its values scatter.
    RangeTrigger t;
    CHECK(feed(t, {500, 30, 500, 180, 500, 90, 500, 290, 40, 500, 210, 500, 120, 60, 500}) == 0);
  }
  {  // A real pass seen in only three readings in a row starts it; so does a slowly approaching object.
    RangeTrigger t, slow;
    CHECK(feed(t, {500, 500, 250, 240, 230, 500, 500}) == 1);
    CHECK(feed(slow, {400, 290, 280, 270, 260}) == 1);
  }
  {  // Fired: the object has to go away (and be seen going away) before the next start.
    RangeTrigger t;
    CHECK(feed(t, {250, 250, 250}) == 1);
    CHECK(feed(t, {100, 100, 100, 100, 100, 100}) == 0 && t.fired());
    // 310 is within the hysteresis (not "away"), 320 and beyond is.
    CHECK(feed(t, {310, 310, 310, 310, 310}) == 0 && t.fired());
    CHECK(feed(t, {320, 320}) == 0 && t.fired());
    CHECK(feed(t, {320}) == 0 && !t.fired());
    CHECK(feed(t, {250, 250, 250}) == 1);
  }
  {  // No echo counts as away, and never as an object.
    RangeTrigger t;
    CHECK(feed(t, {kNone, kNone, kNone, kNone, kNone, kNone}) == 0);
    CHECK(feed(t, {250, 250, 250}) == 1);
    CHECK(feed(t, {kNone, kNone}) == 0 && t.fired());
    CHECK(feed(t, {kNone}) == 0 && !t.fired());
  }
  {  // After a pause in ranging the old readings do not count: two before, one after is not three.
    RangeTrigger t;
    CHECK(feed(t, {250, 250}) == 0);
    t.clearWindow();
    CHECK(feed(t, {250}) == 0 && !t.fired());
    CHECK(feed(t, {250, 250}) == 1);
    t.clearWindow();  // The fired state stays: the object still has to go away first.
    CHECK(t.fired() && feed(t, {250, 250, 250}) == 0);
  }
  {  // The trigger distance may change between readings.
    RangeTrigger t;
    CHECK(feed(t, {400, 400, 400}, 300) == 0);
    CHECK(feed(t, {400, 400, 400}, 500) == 1);
  }
  {  // Near the sensor the 20 % tolerance is the 40 mm floor, far away it grows.
    RangeTrigger near, far;
    RangeTrigger near45;
    CHECK(feed(near, {60, 100, 90}, 300) == 1);       // 40 mm apart is allowed ...
    CHECK(feed(near45, {60, 105, 90}, 300) == 0);     // ... 45 is not
    CHECK(feed(far, {900, 1000, 1080}, 1200) == 1);   // 180 mm apart, 20 % of 1080 = 216
  }
}
void touchCalChecks() {
  using namespace touchcal;
  // The panel reports y 12 px too low and x scaled by 0.95: the fit recovers the inverse.
  Point measured[kTargets];
  for (std::size_t i = 0; i < kTargets; ++i) measured[i] = {kTarget[i].x * 0.95f, kTarget[i].y + 12};
  const TouchCal cal = fit(kTarget, measured, kTargets);
  CHECK(std::fabs(cal.ax - 1 / 0.95f) < 0.001f && std::fabs(cal.bx) < 0.05f);
  CHECK(std::fabs(cal.ay - 1) < 0.001f && std::fabs(cal.by + 12) < 0.05f);
  CHECK(plausible(cal));
  CHECK(std::fabs(cal.y(measured[3].y) - kTarget[3].y) < 0.05f);
  // Identity when the measurements carry no information (all taps in one place).
  Point same[kTargets];
  for (auto &p : same) p = {100, 100};
  const TouchCal flat = fit(kTarget, same, kTargets);
  CHECK(flat.ax == 1 && flat.bx == 0 && flat.ay == 1 && flat.by == 0);
  CHECK(fit(kTarget, measured, 1).ax == 1);
  // A tap on the wrong target gives an implausible fit.
  Point wrong[kTargets];
  for (std::size_t i = 0; i < kTargets; ++i) wrong[i] = kTarget[kTargets - 1 - i];
  CHECK(!plausible(fit(kTarget, wrong, kTargets)));
  CHECK(plausible(TouchCal{}));
}

void liveSpectrumChecks() {
  static LiveSpectrum live;  // ~25 KB: off the stack.
  // 40 Hz, 1.5 m/s2 on Y at 490 Hz (the measured rate of this board), microsecond times.
  const auto feed = [](LiveSpectrum &l, int n, int from, float hz, int axis) {
    for (int i = from; i < from + n; ++i) {
      const float t = i / 490.0f, v = 1.5f * std::sin(2 * 3.14159265f * hz * t);
      l.add(Accel{axis == 0 ? v : 0.01f, axis == 1 ? v : 0.0f, axis == 2 ? v : 0.0f}, uint32_t(i * 1e6f / 490.0f));
    }
  };
  feed(live, 1000, 0, 40, 1);
  CHECK(!live.ready());
  live.compute();
  CHECK(!live.valid());  // Not until the window is full.
  feed(live, 100, 1000, 40, 1);
  CHECK(live.ready());
  live.compute();
  CHECK(live.valid() && std::fabs(live.rateHz() - 490.0f) < 0.5f && std::fabs(live.nyquistHz() - 245.0f) < 0.3f);
  CHECK(std::fabs(live.domHz() - 40.0f) < 0.5f && std::fabs(live.domAmp() - 1.5f) < 0.1f);
  // The line is in Y's columns, not in X's or Z's; column 40/245*96 = 15.7.
  const int c = int(40.0f / 245.0f * LiveSpectrum::kCols);
  float ymax = 0, xmax = 0, zmax = 0;
  for (int k = c - 1; k <= c + 1; ++k) {
    ymax = fmaxf(ymax, live.column(1, k));
    xmax = fmaxf(xmax, live.column(0, k));
    zmax = fmaxf(zmax, live.column(2, k));
  }
  CHECK(ymax > 1.3f && xmax < 0.05f && zmax < 0.05f && live.top() >= ymax);
  // The window slides: the signal moves to 90 Hz on Z, and after a full window it is all that is seen.
  feed(live, int(LiveSpectrum::kSize), 1100, 90, 2);
  live.compute();
  CHECK(std::fabs(live.domHz() - 90.0f) < 0.5f && live.column(1, c) < 0.05f);
  // A gap clears it.
  live.clear();
  CHECK(!live.ready() && !live.valid());
  // Silence: no line, the scale floor holds.
  feed(live, int(LiveSpectrum::kSize), 0, 0, 1);
  live.compute();
  CHECK(live.valid() && live.domHz() == 0 && live.top() == 0.05f);
}

void vl53Checks() {
  uint8_t b[vl53::kResultBytes] = {};
  b[0] = uint8_t(11 << 3);
  b[10] = 0x01;
  b[11] = 0x2C;  // 300 mm
  CHECK(vl53::decodeRange(b) == 300);
  b[0] = uint8_t(4 << 3);  // Phase failure: not a range.
  CHECK(vl53::decodeRange(b) == RangeTrigger::kNoReading);
  b[0] = uint8_t(11 << 3 | 0x01);  // Other bits of byte 0 do not matter.
  CHECK(vl53::decodeRange(b) == 300);
  b[10] = 0x1F;
  b[11] = 0xFE;  // 8190: nothing in range.
  CHECK(vl53::decodeRange(b) == RangeTrigger::kNoReading);
  uint8_t amb[vl53::kResultBytes] = {};
  amb[8] = 0x04;
  amb[9] = 0x80;  // 0x0480 / 128 = 9.0 Mcps: the blind state seen on the device
  CHECK(std::fabs(vl53::ambientMcps(amb) - 9.0f) < 0.01f && vl53::ambientMcps(amb) > vl53::kBlindAmbientMcps);
  amb[8] = 0x00;
  amb[9] = 0x20;  // 0.25 Mcps: a healthy sensor indoors
  CHECK(std::fabs(vl53::ambientMcps(amb) - 0.25f) < 0.01f && vl53::ambientMcps(amb) < vl53::kBlindAmbientMcps);
  CHECK(vl53::resultReady(0x04) && !vl53::resultReady(0x00) && !vl53::resultReady(0x18));
}

void uiChecks() {
  using namespace rangeui;
  Settings s;
  CHECK(s.distanceMm == 300 && s.recordS == 5 && s.preRollMs == 1000 && s.mode == 1);
  s.distanceMm = 300;
  CHECK(step(s, Param::Distance, +1).distanceMm == 310);
  s.distanceMm = 490;
  CHECK(step(s, Param::Distance, +1).distanceMm == 500);
  s.distanceMm = 500;
  CHECK(step(s, Param::Distance, +1).distanceMm == 550);
  CHECK(step(s, Param::Distance, -1).distanceMm == 490);
  s.distanceMm = 550;
  CHECK(step(s, Param::Distance, -1).distanceMm == 500);
  s.distanceMm = 30;
  CHECK(step(s, Param::Distance, -1).distanceMm == 30);
  s.distanceMm = 1200;
  CHECK(step(s, Param::Distance, +1).distanceMm == 1200);
  s.recordS = 30;
  CHECK(step(s, Param::Record, +1).recordS == 30);
  s.recordS = 1;
  CHECK(step(s, Param::Record, -1).recordS == 1);
  CHECK(step(s, Param::Record, +1).recordS == 2);
  s.preRollMs = 1000;
  CHECK(step(s, Param::PreRoll, +1).preRollMs == 1000);
  CHECK(step(s, Param::PreRoll, -1).preRollMs == 900);
  s.preRollMs = 0;
  CHECK(step(s, Param::PreRoll, -1).preRollMs == 0);
  s.mode = 2;
  CHECK(step(s, Param::Mode, +1).mode == 2 && step(s, Param::Mode, -1).mode == 1);
  s.mode = 0;
  CHECK(step(s, Param::Mode, -1).mode == 0);
  const Settings wild = clamp(Settings{5000, -3, 99999, 7});
  CHECK(wild.distanceMm == 1200 && wild.recordS == 1 && wild.preRollMs == 1000 && wild.mode == 2);
  CHECK(budgetUs(0) == 20000 && budgetUs(1) == 33000 && budgetUs(2) == 200000);
  CHECK(chartRangeMs2(0.5f) == 5.0f && chartRangeMs2(5.0f) == 5.0f && chartRangeMs2(5.1f) == 10.0f);
  CHECK(chartRangeMs2(100.0f) == 80.0f);
  // Hit testing: the second row (Record) is at y 78..115.
  CHECK(hit(10, 90).dir == -1 && hit(10, 90).param == Param::Record);
  CHECK(hit(300, 90).dir == +1 && hit(300, 90).param == Param::Record);
  CHECK(hit(160, 90).dir == 0);
  CHECK(hit(300, 40).param == Param::Distance && hit(300, 40).dir == +1);
  CHECK(hit(10, 170).param == Param::Mode && hit(10, 170).dir == -1);
  CHECK(hit(10, 20).dir == 0 && hit(10, 205).dir == 0);
  {
    const Press minus{Param::Distance, -1}, plus{Param::Distance, +1}, plusRec{Param::Record, +1};
    HoldRepeater h;
    CHECK(h.update(minus, 0) == -1);
    CHECK(h.update(minus, 100) == 0);
    CHECK(h.update(minus, 499) == 0);
    CHECK(h.update(minus, 500) == -1);
    CHECK(h.update(minus, 550) == 0);
    CHECK(h.update(minus, 600) == -1);
    CHECK(h.update(Press{}, 610) == 0);
    CHECK(h.update(minus, 620) == -1);
    CHECK(h.update(plus, 700) == +1);
    CHECK(h.update(plus, 1200) == +1);
    CHECK(h.update(plusRec, 1210) == +1);  // Another row: a step right away.
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

// Drives CardHandover like main.cpp does, with a fake gate and writer.
struct HandoverRig {
  CardHandover h;
  CardGate gate;
  bool coreUsb = false;     // RecorderCore reaches Usb one loop after the request.
  bool usbRequested = false, parked = false;
  int parks = 0, unparks = 0, releaseCalls = 0;
  bool inflight = false;    // A USB transfer is inside the gate.
  bool violation = false;   // unpark while a transfer runs, or without a park.

  CardHandover::Actions loop(bool hostOwns) {
    coreUsb = usbRequested;
    const CardHandover::Actions a = h.step(hostOwns, coreUsb, [&] {
      ++releaseCalls;
      return gate.release();
    });
    if (a.grant) gate.grant();
    if (a.usbOn) usbRequested = true;
    if (a.park) { ++parks; parked = true; }
    if (a.unpark) {
      if (inflight || !parked || unparks >= parks) violation = true;
      ++unparks;
      parked = false;
    }
    if (a.usbOff) usbRequested = false;
    return a;
  }
  // A host transfer attempt; true when it may touch the card (then in flight).
  bool startTransfer() {
    if (!gate.enter()) return false;
    if (!parked) { gate.leave(); return false; }
    inflight = true;
    return true;
  }
  void endTransfer() { inflight = false; gate.leave(); }
};

static void handoverChecks() {
  {  // Plain connect -> eject -> disconnect cycle.
    HandoverRig r;
    CardHandover::Actions a = r.loop(true);
    CHECK(a.usbOn && a.grant && !a.park);
    a = r.loop(true);
    CHECK(a.park && !a.usbOn);
    CHECK(r.loop(true).grant && r.parks == 1);
    CHECK(r.startTransfer());
    r.endTransfer();
    a = r.loop(false);  // Eject.
    CHECK(a.unpark && a.usbOff && r.unparks == 1);
    a = r.loop(false);  // Disconnect: nothing more.
    CHECK(!a.unpark && !a.usbOff && !a.grant && !a.park);
    CHECK(!r.startTransfer() && !r.violation);
  }
  {  // Host re-accesses after the eject while the release is pending.
    HandoverRig r;
    r.loop(true);
    r.loop(true);
    CHECK(r.parked);
    CHECK(r.startTransfer());     // Access after eject: transfer runs...
    CardHandover::Actions a = r.loop(false);  // ...main saw hostOwns false first.
    CHECK(!a.unpark && !a.usbOff && r.releaseCalls == 1);
    r.endTransfer();
    a = r.loop(true);             // onAccess gave the card back to the host.
    CHECK(a.grant && !a.usbOn && !a.unpark && !a.park);
    CHECK(r.startTransfer());     // Not left revoked.
    r.endTransfer();
    CHECK(r.loop(true).grant && r.parks == 1 && r.unparks == 0 && !r.violation);
  }
  {  // Rapid flap true -> false -> true before the park.
    HandoverRig r;
    CHECK(r.loop(true).usbOn);
    CardHandover::Actions a = r.loop(false);
    CHECK(a.usbOff && !a.unpark && !a.park);  // Never parked: nothing to unpark.
    a = r.loop(true);
    CHECK(a.usbOn && a.grant && !a.park);
    CHECK(r.loop(true).park);
    CHECK(r.startTransfer());
    r.endTransfer();
    CHECK(r.parks == 1 && r.unparks == 0 && !r.violation);
  }
  {  // Cable pulled while a transfer is running.
    HandoverRig r;
    r.loop(true);
    r.loop(true);
    CHECK(r.startTransfer());
    for (int i = 0; i < 5; ++i) {
      const CardHandover::Actions a = r.loop(false);
      CHECK(!a.unpark && !a.usbOff);
    }
    CHECK(r.releaseCalls == 5);
    r.endTransfer();
    CHECK(!r.startTransfer());    // Revoked: refused while releasing.
    CardHandover::Actions a = r.loop(false);
    CHECK(a.unpark && a.usbOff && r.unparks == 1 && !r.violation);
    CHECK(!r.startTransfer());
    a = r.loop(true);             // Re-plugged later: works again.
    CHECK(a.usbOn && a.grant);
    r.loop(true);
    CHECK(r.startTransfer());
    r.endTransfer();
    CHECK(r.parks == 2 && !r.violation);
  }
}

// Simulated sensor FIFO: samples made at trueHz, read every readMs (with jitter),
// at most 128 kept (stream mode drops the oldest). Returns every timestamp given.
struct FifoSim {
  std::vector<uint32_t> stamps;
  uint32_t missedTotal = 0;
  std::size_t lostTrue = 0;  // Samples the FIFO really dropped.
};
static FifoSim runFifo(FifoClock &clock, float trueHz, float seconds, uint32_t readUs,
                       uint32_t stallFromUs = 0, uint32_t stallUs = 0) {
  FifoSim sim;
  const double period = 1e6 / trueHz;
  uint64_t made = 0, taken = 0;
  uint32_t seed = 12345;
  for (uint32_t t = readUs; t < uint32_t(seconds * 1e6); t += readUs) {
    if (stallUs && t >= stallFromUs && t < stallFromUs + stallUs) continue;
    seed = seed * 1103515245u + 12345u;
    const uint32_t jitter = (seed >> 16) % 400;  // 0..0.4 ms late
    const uint32_t now = t + jitter;
    made = uint64_t(double(now) / period);
    uint64_t n = made - taken;
    if (n > 128) {
      sim.lostTrue += n - 128;
      n = 128;
    }
    taken = made;
    const FifoClock::Batch b = clock.batch(now, uint32_t(n));
    sim.missedTotal += b.missed;
    for (uint32_t i = 0; i < n; ++i) sim.stamps.push_back(b.at(i));
  }
  return sim;
}

static void fifoClockChecks() {
  {  // exact 2000 Hz, reads every 5 ms with jitter: uniform 500 us steps, nothing lost
    FifoClock clock(2000);
    const FifoSim s = runFifo(clock, 2000, 6.0f, 5000);
    CHECK(s.missedTotal == 0 && s.lostTrue == 0);
    bool uniform = true;
    for (std::size_t i = 1; i < s.stamps.size(); ++i) {
      const int32_t d = int32_t(s.stamps[i] - s.stamps[i - 1]);
      if (d < 490 || d > 510) uniform = false;
    }
    CHECK(uniform);
    CHECK(std::fabs(clock.rateHz() - 2000) < 5 && clock.rateMeasured());
  }
  {  // chip 3 % slow: the measured rate follows, steps settle near 515 us
    FifoClock clock(2000);
    const FifoSim s = runFifo(clock, 1940, 8.0f, 5000);
    CHECK(s.missedTotal == 0);
    CHECK(std::fabs(clock.rateHz() - 1940) < 5);
    const std::size_t n = s.stamps.size();
    CHECK(n > 100);
    if (n > 100) {
      const double avg = double(s.stamps[n - 1] - s.stamps[n - 101]) / 100;
      CHECK(avg > 510 && avg < 520);
    }
  }
  {  // 200 ms without reads: the FIFO overflows, the loss is reported, time jumps
    FifoClock clock(2000);
    const FifoSim s = runFifo(clock, 2000, 4.0f, 5000, 2000000, 200000);
    CHECK(s.lostTrue > 0);
    CHECK(s.missedTotal + 3 >= s.lostTrue && s.missedTotal <= s.lostTrue + 3);
    bool jump = false;
    for (std::size_t i = 1; i < s.stamps.size(); ++i)
      if (int32_t(s.stamps[i] - s.stamps[i - 1]) > 30000) jump = true;
    CHECK(jump);
  }
  {  // empty batch changes nothing
    FifoClock clock(2000);
    clock.batch(1000000, 10);
    const FifoClock::Batch e = clock.batch(1002000, 0);
    const FifoClock::Batch b = clock.batch(1005000, 10);
    CHECK(e.missed == 0 && b.missed == 0 && e.firstUs == b.firstUs);
  }
}

// Polled sensor with its own sample counter: chip at trueHz, polls every 1 ms with
// jitter, sometimes a stall (samples lost, but counted exactly by the chip).
static void countedClockChecks() {
  CountedClock clock(500);
  const double period = 1e6 / 512.0;  // Internal oscillator 2.4 % fast.
  uint64_t lastIndex = 0;
  bool first = true;
  uint32_t seed = 99, prevT = 0;
  std::vector<int32_t> steps;
  std::vector<uint32_t> skippedAt;
  for (uint32_t poll = 0; poll < 30000; ++poll) {  // 30 s
    seed = seed * 1103515245u + 12345u;
    if (poll % 2000 >= 1000 && poll % 2000 < 1005) continue;  // a 5 ms stall every 2 s
    const uint32_t now = poll * 1000 + (seed >> 16) % 300;
    const uint64_t index = uint64_t(double(now) / period);
    if (!first && index == lastIndex) continue;
    const uint32_t skipped = first ? 0 : uint32_t(index - lastIndex - 1);
    const uint32_t t = clock.stamp(now, skipped);
    if (!first) {
      steps.push_back(int32_t(t - prevT));
      skippedAt.push_back(skipped);
    }
    first = false;
    lastIndex = index;
    prevT = t;
  }
  CHECK(clock.rateMeasured() && std::fabs(clock.rateHz() - 512.0f) < 0.5f);
  // After the rate is known (last 10 s): steps are one or (skipped+1) real periods.
  bool ok = true;
  std::size_t lossy = 0;
  for (std::size_t i = steps.size() * 2 / 3; i < steps.size(); ++i) {
    const double want = period * (skippedAt[i] + 1);
    if (std::fabs(steps[i] - want) > 40) ok = false;
    lossy += skippedAt[i] > 0;
  }
  CHECK(ok);
  CHECK(lossy > 0);  // the stalls really lost samples, and they show as longer steps
}

static void statsChecks() {
  // 3000 samples at 500 Hz: X = 9.8 + 2 sin(2 pi 50 t), Y = 1 sin(2 pi 20 t), Z = 0.
  std::vector<Accel> v;
  for (int i = 0; i < 3000; ++i) {
    const float t = i / 500.0f;
    v.push_back({9.8f + 2 * std::sin(2 * 3.14159265f * 50 * t), std::sin(2 * 3.14159265f * 20 * t), 0});
  }
  const MeasStats st = measstats::compute(v.data(), v.size(), 500.0f);
  CHECK(st.valid && st.samples == 3000);
  CHECK(std::fabs(st.durationS - 5.998f) < 0.01f);
  CHECK(std::fabs(st.axis[0].mean - 9.8f) < 0.02f);
  CHECK(std::fabs(st.axis[0].rms - 2 / std::sqrt(2.0f)) < 0.02f);
  CHECK(std::fabs(st.axis[0].peak - 2.0f) < 0.15f);
  CHECK(std::fabs(st.axis[0].p2p - 4.0f) < 0.3f);
  CHECK(std::fabs(st.axis[1].rms - 1 / std::sqrt(2.0f)) < 0.02f);
  CHECK(st.axis[2].rms == 0 && st.axis[2].p2p == 0);
  CHECK(st.hasSpectrum && std::fabs(st.domHz - 50.0f) < 1.0f);
  CHECK(std::fabs(st.domAmp - 2.0f) < 0.2f);
  CHECK(std::fabs(st.nyquistHz - 250.0f) < 0.01f);
  // The strongest column is the 50 Hz one (50 / 250 * 128 = column 25.6).
  std::size_t top = 0;
  for (std::size_t c = 0; c < MeasStats::kSpectrumCols; ++c)
    if (st.spectrum[c] > st.spectrum[top]) top = c;
  CHECK(top == 25);

  // The lines table: 50 Hz (2.0) then 20 Hz (1.0), interpolated between bins.
  CHECK(st.peakCount >= 2);
  CHECK(std::fabs(st.peaks[0].hz - 50.0f) < 0.3f && std::fabs(st.peaks[0].amp - 2.0f) < 0.1f);
  CHECK(std::fabs(st.peaks[1].hz - 20.0f) < 0.3f && std::fabs(st.peaks[1].amp - 1.0f) < 0.1f);
  for (int i = 1; i < st.peakCount; ++i) CHECK(st.peaks[i - 1].amp >= st.peaks[i].amp);
  CHECK(std::fabs(st.binHz - 500.0f / 1024) < 1e-4f && st.fft[100] >= 0);
  // Per axis: X carries the 50 Hz line, Y the 20 Hz one, Z (constant) nothing.
  CHECK(st.axisPeakCount[0] == 1 && std::fabs(st.axisPeaks[0][0].hz - 50.0f) < 0.3f &&
        std::fabs(st.axisPeaks[0][0].amp - 2.0f) < 0.1f);
  CHECK(st.axisPeakCount[1] == 1 && std::fabs(st.axisPeaks[1][0].hz - 20.0f) < 0.3f &&
        std::fabs(st.axisPeaks[1][0].amp - 1.0f) < 0.1f);
  CHECK(st.axisPeakCount[2] == 0);
  CHECK(st.axisFft[0][100] >= 0 && st.axisFft[0][int(50.0f / st.binHz + 0.5f)] > 1.5f);
  {  // Rollers on different axes: top rollers 33 Hz on Z, side rollers 47 Hz on X and 12 Hz weak
     // on Y. A line far below the strongest one (1 %) is left out, the weak real one stays.
    std::vector<Accel> w;
    for (int i = 0; i < 3000; ++i) {
      const float t = i / 500.0f;
      w.push_back({1.5f * std::sin(2 * 3.14159265f * 47 * t), 0.3f * std::sin(2 * 3.14159265f * 12 * t) +
                       0.01f * std::sin(2 * 3.14159265f * 90 * t), 3.0f * std::sin(2 * 3.14159265f * 33 * t) + 9.8f});
    }
    const MeasStats r = measstats::compute(w.data(), w.size(), 500.0f);
    CHECK(r.axisPeakCount[0] == 1 && std::fabs(r.axisPeaks[0][0].hz - 47.0f) < 0.3f);
    CHECK(r.axisPeakCount[1] == 1 && std::fabs(r.axisPeaks[1][0].hz - 12.0f) < 0.3f);
    CHECK(r.axisPeakCount[2] == 1 && std::fabs(r.axisPeaks[2][0].hz - 33.0f) < 0.3f &&
          std::fabs(r.axisPeaks[2][0].amp - 3.0f) < 0.25f);
  }
  {  // Many tones: only the five strongest, in order.
    std::vector<Accel> w;
    for (int i = 0; i < 3000; ++i) {
      const float t = i / 500.0f;
      float x = 0;
      for (int tone = 1; tone <= 8; ++tone) x += tone * 0.3f * std::sin(2 * 3.14159265f * 15 * tone * t);
      w.push_back({x, 0, 0});
    }
    const MeasStats many = measstats::compute(w.data(), w.size(), 500.0f);
    CHECK(many.peakCount == 5);
    CHECK(std::fabs(many.peaks[0].hz - 120.0f) < 0.5f && std::fabs(many.peaks[4].hz - 60.0f) < 0.5f);
  }
  {  // A strong 1.2 Hz bounce (strongest bin on a falling slope, no local maximum) and a
     // weak 30 Hz line: the dominant line stays at the strongest bin, not at 30 Hz.
    std::vector<Accel> w;
    for (int i = 0; i < 3000; ++i) {
      const float t = i / 500.0f;
      w.push_back({3.0f * std::sin(2 * 3.14159265f * 1.2f * t) + 0.3f * std::sin(2 * 3.14159265f * 30 * t), 0, 0});
    }
    const MeasStats low = measstats::compute(w.data(), w.size(), 500.0f);
    CHECK(low.domHz < 3.0f && low.domAmp > 0.3f);
  }

  // Too short for the FFT: statistics only. Empty input: not valid.
  const MeasStats shortStats = measstats::compute(v.data(), 500, 500.0f);
  CHECK(shortStats.valid && !shortStats.hasSpectrum);
  CHECK(!measstats::compute(v.data(), 1, 500.0f).valid && !measstats::compute(nullptr, 0, 0).valid);

  // Capture: tee through the sink, then read it back.
  struct Null : MeasSink {
    bool accept = true;
    int writes = 0;
    bool open(int) override { return accept; }
    void write(int32_t, const Accel &) override { ++writes; }
    void close() override {}
  } inner;
  static Accel storage[MeasCapture::kCapacity];
  MeasCapture cap;
  CapturingSink sink(inner, cap);
  MeasCapture::View view;
  CHECK(!cap.tryAcquire(0, view));
  cap.attach(storage);
  CHECK(sink.open(20));
  for (int i = 0; i < 1000; ++i) sink.write(i * 2000, {float(i), 0, 0});  // 500 Hz
  sink.close();
  CHECK(inner.writes == 1000);
  CHECK(cap.tryAcquire(0, view) && view.count == 1000 && view.seq == 1);
  CHECK(std::fabs(view.rateHz - 500.0f) < 0.1f && view.data[999].x == 999);
  // Held by the reader: the next measurement is written but not captured.
  CHECK(sink.open(20));
  sink.write(0, {1, 2, 3});
  sink.close();
  CHECK(inner.writes == 1001);
  CHECK(view.count == 1000 && view.data[5].x == 5);
  CHECK(!cap.tryAcquire(view.seq, view));  // Nothing new.
  cap.release();
  CHECK(sink.open(20));
  for (int i = 0; i < 20000; ++i) sink.write(i * 2000, {1, 0, 0});  // More than the capacity.
  sink.close();
  CHECK(cap.tryAcquire(1, view) && view.count == MeasCapture::kCapacity && view.seq == 2);
  cap.release();
  inner.accept = false;
  CHECK(!sink.open(20));  // The card refused: nothing captured, no new sequence number.
  CHECK(!cap.tryAcquire(2, view));
}

static void peakLogChecks() {
  PeakLog<4> log;
  CHECK(log.size() == 0 && log.maxPeak() == 0 && log.firstIndex() == 1);
  for (unsigned i = 1; i <= 3; ++i) log.add({i, float(i) * 1.5f, {float(i), 0, 0}});
  CHECK(log.size() == 3 && log.at(0).number == 1 && log.at(2).number == 3);
  CHECK(std::fabs(log.maxPeak() - 4.5f) < 1e-5f);
  for (unsigned i = 4; i <= 6; ++i) log.add({i, i == 5 ? 20.0f : 1.0f, {}});
  CHECK(log.size() == 4 && log.at(0).number == 3 && log.at(3).number == 6);  // 1 and 2 dropped.
  CHECK(log.firstIndex() == 3 && log.maxPeak() == 20.0f);

  using peakchart::niceCeil;
  CHECK(niceCeil(0) == 1.0f && niceCeil(-3) == 1.0f);
  CHECK(std::fabs(niceCeil(0.3f) - 0.5f) < 1e-6f || std::fabs(niceCeil(0.3f) - 0.5f) < 1e-4f);
  CHECK(std::fabs(niceCeil(1.0f) - 1.0f) < 1e-5f && std::fabs(niceCeil(1.01f) - 2.0f) < 1e-5f);
  CHECK(std::fabs(niceCeil(2.3f) - 2.5f) < 1e-5f && std::fabs(niceCeil(4.2f) - 5.0f) < 1e-5f);
  CHECK(std::fabs(niceCeil(7.0f) - 10.0f) < 1e-4f && std::fabs(niceCeil(31.0f) - 50.0f) < 1e-3f);
  CHECK(std::fabs(peakchart::yRange(0.02f) - 0.5f) < 1e-5f);  // The floor.
  CHECK(peakchart::yRange(9.8f) >= 9.8f && peakchart::yRange(9.8f) <= 20.0f);
  CHECK(peakchart::labelStep(1, 6) == 1 && peakchart::labelStep(6, 6) == 1);
  CHECK(peakchart::labelStep(7, 6) == 2 && peakchart::labelStep(12, 6) == 2);
  CHECK(peakchart::labelStep(13, 6) == 5 && peakchart::labelStep(30, 6) == 5);
  CHECK(peakchart::labelStep(31, 6) == 10 && peakchart::labelStep(512, 6) == 100);
}

// Feeds (x, y) points every 10 ms (x < 0: no contact) and returns the last result
// that had a gesture or a rejection.
// Feeds (x, y) points stepMs apart (x < 0: no contact) and returns the last result
// that had a gesture or a rejection.
static SwipeGuard::Result feedSwipe(SwipeGuard &g, uint32_t &t, std::vector<std::pair<int, int>> path,
                                    uint32_t stepMs = 10) {
  for (int i = 0; i < 4; ++i) path.push_back({-1, -1});  // Release.
  SwipeGuard::Result last;
  for (const auto &p : path) {
    TouchSample s;
    s.down = p.first >= 0;
    s.x = int16_t(p.first);
    s.y = int16_t(p.second);
    const SwipeGuard::Result r = g.update(s, t);
    if (r.gesture || r.rejected) last = r;
    t += stepMs;
  }
  return last;
}

static void swipeGuardChecks() {
  SwipeGuard g;
  uint32_t t = 1000;
  // A real swipe left: 200 px over 120 ms.
  std::vector<std::pair<int, int>> path;
  for (int i = 0; i <= 12; ++i) path.push_back({260 - i * 17, 120});
  SwipeGuard::Result r = feedSwipe(g, t, path);
  CHECK(r.gesture.type == GestureType::SwipeLeft && !r.rejected);
  // The same swipe seen only every 40 ms (late samples): 68 px steps, still accepted.
  path.clear();
  for (int i = 0; i <= 3; ++i) path.push_back({260 - i * 68, 120});
  r = feedSwipe(g, t, path, 40);
  CHECK(r.gesture.type == GestureType::SwipeLeft && !r.rejected);
  // A short swipe of 38 px (above the 35 px minimum).
  r = feedSwipe(g, t, {{100, 100}, {110, 100}, {120, 101}, {130, 101}, {138, 102}}, 25);
  CHECK(r.gesture.type == GestureType::SwipeRight && !r.rejected);
  // A real quick swipe seen on the device: 56 px in 70 ms. Accepted (it was rejected at a 100 ms minimum).
  r = feedSwipe(g, t, {{173, 101}, {165, 101}, {157, 101}, {149, 101}, {141, 101}, {133, 101}, {125, 101}, {117, 100}});
  CHECK(r.gesture.type == GestureType::SwipeLeft && !r.rejected && r.contactMs == 70);
  // 50 ms of contact is too short for a swipe.
  r = feedSwipe(g, t, {{173, 101}, {160, 101}, {147, 101}, {134, 101}, {121, 101}, {110, 100}});
  CHECK(!r.gesture && r.rejected);
  CHECK(SwipeGuard::tapValid(30, 0) && SwipeGuard::tapValid(120, 1) && !SwipeGuard::tapValid(10, 0) &&
        !SwipeGuard::tapValid(100, 40));
  // Phantom touch: a point, then a jump to the clamped edge, then gone.
  r = feedSwipe(g, t, {{60, 120}, {60, 120}, {319, 118}, {319, 118}, {319, 118}});
  CHECK(!r.gesture && r.rejected && r.maxSpeed > SwipeGuard::kMaxSpeedPxPerMs);
  // Phantom contact of a single sample pair 10 ms apart: too short.
  r = feedSwipe(g, t, {{40, 100}, {100, 100}});
  CHECK(!r.gesture && r.rejected);
  // A real swipe right after that still works.
  path.clear();
  for (int i = 0; i <= 10; ++i) path.push_back({50 + i * 20, 140});
  r = feedSwipe(g, t, path);
  CHECK(r.gesture.type == GestureType::SwipeRight && !r.rejected && r.contactMs == 100);
  // Button presses: a real finger holds still long enough and does not jump.
  CHECK(!SwipeGuard::pressValid(10, 0) && !SwipeGuard::pressValid(50, 1));
  CHECK(SwipeGuard::pressValid(60, 0) && SwipeGuard::pressValid(300, 3));
  CHECK(!SwipeGuard::pressValid(300, 40));
  using UB = usbui::Button;
  CHECK(usbui::hit(160, 60) == UB::Usb && usbui::hit(160, 135) == UB::Calibrate && usbui::hit(160, 180) == UB::Clean);
  CHECK(usbui::hit(160, 100) == UB::None && usbui::hit(5, 60) == UB::None && usbui::hit(160, 205) == UB::None);
  // The controller drops the finger for two samples mid-swipe: still one swipe, not two taps.
  r = feedSwipe(g, t, {{260, 120}, {240, 120}, {220, 120}, {200, 120}, {-1, -1}, {-1, -1}, {160, 121}, {140, 121},
                       {120, 121}, {100, 122}, {80, 122}});
  CHECK(r.gesture.type == GestureType::SwipeLeft && !r.rejected && r.gesture.dx == -180);
  // A real lift (four samples without contact) ends it: two separate contacts.
  {
    SwipeGuard g2;
    uint32_t t2 = 5000;
    int swipesSeen = 0;
    const std::vector<std::pair<int, int>> path = {{260, 120}, {250, 120}, {240, 120}, {-1, -1}, {-1, -1}, {-1, -1},
                                                   {-1, -1}, {100, 120}, {100, 120}, {100, 120}};
    for (const auto &p : path) {
      TouchSample s;
      s.down = p.first >= 0;
      s.x = int16_t(p.first);
      s.y = int16_t(p.second);
      swipesSeen += g2.update(s, t2).gesture.type == GestureType::SwipeLeft;
      t2 += 10;
    }
    CHECK(swipesSeen == 0);  // 20 px, then a separate touch: no swipe out of the two together.
  }
  // A tap is passed through.
  r = feedSwipe(g, t, {{100, 100}, {101, 100}, {101, 101}});
  CHECK(r.gesture.type == GestureType::Tap && !r.rejected);
}

int main() {
  formatChecks();
  ringChecks();
  triggerChecks();
  coreChecks();
  uiChecks();
  gateChecks();
  handoverChecks();
  fifoClockChecks();
  countedClockChecks();
  statsChecks();
  peakLogChecks();
  swipeGuardChecks();
  rangeChecks();
  liveSpectrumChecks();
  touchCalChecks();
  vl53Checks();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("all ok");
  return 0;
}
