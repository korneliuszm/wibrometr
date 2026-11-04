// Host tests for the hardware-independent logic of the Waveshare port:
//   g++ -std=c++17 -Wall -Wextra -Iinclude test/port_checks.cpp -o port_checks && ./port_checks
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include "BatteryModel.h"
#include "Keypad.h"
#include "LogFormat.h"
#include "Qmi8658Decode.h"
#include "SampleClock.h"
#include "TouchGestures.h"

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

bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

// ------------------------------------------------------------------ QMI8658

void testQmiDecode() {
  const uint8_t flat[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10}; // Z = +4096 LSB little endian.
  Accel a = qmi8658::decode(flat, 2);                           // +-8 g: 4096 LSB/g.
  CHECK(near(a.x, 0) && near(a.y, 0) && near(a.z, qmi8658::kGravity), "1 g on Z: %.3f", a.z);
  const uint8_t neg[6] = {0x00, 0xF0, 0xFF, 0xFF, 0x00, 0x80}; // X -4096, Y -1, Z -32768.
  a = qmi8658::decode(neg, 2);
  CHECK(near(a.x, -qmi8658::kGravity) && near(a.y, -qmi8658::kGravity / 4096) && near(a.z, -8 * qmi8658::kGravity),
        "negative values: %.3f %.5f %.3f", a.x, a.y, a.z);
  CHECK(qmi8658::lsbPerG(0) == 16384 && qmi8658::lsbPerG(3) == 2048, "scales");
  CHECK(qmi8658::rangeG(2) == 8 && qmi8658::rangeG(3) == 16, "ranges");
  CHECK(qmi8658::odrCode(500) == 4 && qmi8658::odrCode(1000) == 3 && qmi8658::odrCode(250) == 5, "odr codes");
  CHECK(qmi8658::odrHz(4) == 500 && qmi8658::odrHz(3) == 1000, "odr values");
  CHECK(near(qmi8658::lpfShare(3) * 500, 66.85f, 0.01f), "13.37%% of 500 Hz");
  CHECK(qmi8658::le16(0xFF, 0x7F) == 32767 && qmi8658::le16(0x00, 0x80) == -32768, "int16 limits");
}

// -------------------------------------------------------------- SampleClock

void testClockUniform() {
  // 500 Hz samples seen by a 1 kHz poll with up to 1 ms of jitter.
  std::mt19937 rng(1);
  std::uniform_int_distribution<int> jitter(0, 1000);
  SampleClock clock;
  const uint32_t period = 2000;
  uint32_t prev = 0;
  double maxDev = 0, sumStep = 0;
  int n = 0;
  for (int i = 0; i < 5000; ++i) {
    const uint32_t arrival = 100000 + uint32_t(i) * period + uint32_t(jitter(rng));
    const auto s = clock.stamp(arrival, period);
    CHECK(s.missed == 0, "no gaps expected at %d", i);
    if (i > 0) {
      const double step = double(int32_t(s.us - prev));
      sumStep += step;
      maxDev = std::fmax(maxDev, std::fabs(step - period));
      ++n;
    }
    prev = s.us;
  }
  CHECK(maxDev < 80, "timestamps are uniform, worst step error %.0f us", maxDev);
  CHECK(std::fabs(sumStep / n - period) < 1.0, "mean step %.2f us", sumStep / n);
}

void testClockDriftAndGaps() {
  // The sensor clock runs 0.2% fast: the clock must follow, not run away.
  SampleClock clock;
  const uint32_t nominal = 2000;
  const double actual = 1996;
  uint32_t last = 0;
  for (int i = 0; i < 20000; ++i) {
    const uint32_t arrival = uint32_t(1000 + i * actual);
    last = clock.stamp(arrival, nominal).us;
  }
  const double lag = double(int32_t(uint32_t(1000 + 19999 * actual) - last));
  CHECK(std::fabs(lag) < 3000, "tracks a 0.2%% fast sensor clock, lag %.0f us", lag);
  // Missed samples are counted and the clock re-syncs.
  SampleClock c2;
  c2.stamp(0, 2000);
  c2.stamp(2000, 2000);
  const auto gap = c2.stamp(2000 + 4 * 2000 + 100, 2000); // Samples at 4000, 6000, 8000 missed.
  CHECK(gap.missed == 3, "three missed samples, got %u", gap.missed);
  const auto after = c2.stamp(gap.us + 2000, 2000);
  CHECK(after.missed == 0 && after.us == gap.us + 2000, "continues cleanly after the gap");
  // Wrap of the 32-bit microsecond counter.
  SampleClock c3;
  uint32_t t = 0xFFFFF000u;
  uint32_t prev = c3.stamp(t, 2000).us;
  for (int i = 0; i < 10; ++i) {
    t += 2000;
    const uint32_t cur = c3.stamp(t, 2000).us;
    CHECK(cur - prev == 2000, "uniform across the counter wrap");
    prev = cur;
  }
}

// ------------------------------------------------------------ TouchGestures

TouchSample down(int x, int y) { return {true, int16_t(x), int16_t(y)}; }
TouchSample up() { return {false, 0, 0}; }

Gesture run(GestureDetector &d, const std::vector<TouchSample> &samples, uint32_t startMs, uint32_t stepMs) {
  Gesture result;
  uint32_t t = startMs;
  for (const auto &s : samples) {
    const Gesture g = d.update(s, t);
    if (g) result = g;
    t += stepMs;
  }
  return result;
}

void testTouchMapping() {
  CHECK(mapTouchLandscape(0, 0).x == 0 && mapTouchLandscape(0, 0).y == 239, "raw (0,0) is bottom left");
  CHECK(mapTouchLandscape(239, 319).x == 319 && mapTouchLandscape(239, 319).y == 0, "raw max is top right");
  CHECK(mapTouchLandscape(120, 160).x == 160 && mapTouchLandscape(120, 160).y == 119, "centre");
}

void testGestures() {
  GestureDetector d;
  // Tap: short, almost still.
  Gesture g = run(d, {down(100, 100), down(101, 101), down(101, 100), up(), up()}, 0, 10);
  CHECK(g.type == GestureType::Tap && g.x == 100 && g.y == 100, "tap");
  // Swipe left: finger moves 120 px to the left in 150 ms.
  std::vector<TouchSample> left;
  for (int i = 0; i < 10; ++i) left.push_back(down(250 - i * 13, 100 + (i % 2)));
  left.push_back(up());
  left.push_back(up());
  g = run(d, left, 1000, 15);
  CHECK(g.type == GestureType::SwipeLeft && g.dx < -100, "swipe left (dx %d)", g.dx);
  std::vector<TouchSample> right;
  for (int i = 0; i < 10; ++i) right.push_back(down(60 + i * 13, 120));
  right.push_back(up());
  right.push_back(up());
  g = run(d, right, 2000, 15);
  CHECK(g.type == GestureType::SwipeRight && g.dx > 100, "swipe right");
  std::vector<TouchSample> vertical;
  for (int i = 0; i < 8; ++i) vertical.push_back(down(160, 200 - i * 12));
  vertical.push_back(up());
  vertical.push_back(up());
  g = run(d, vertical, 3000, 15);
  CHECK(g.type == GestureType::SwipeUp, "swipe up");
  // Diagonal drag: neither tap nor swipe.
  std::vector<TouchSample> diagonal;
  for (int i = 0; i < 8; ++i) diagonal.push_back(down(100 + i * 10, 100 + i * 9));
  diagonal.push_back(up());
  diagonal.push_back(up());
  g = run(d, diagonal, 4000, 15);
  CHECK(g.type == GestureType::None, "diagonal drag is ignored, got %d", int(g.type));
  // Slow drag: too long for a swipe.
  std::vector<TouchSample> slow;
  for (int i = 0; i < 20; ++i) slow.push_back(down(250 - i * 10, 100));
  slow.push_back(up());
  slow.push_back(up());
  g = run(d, slow, 5000, 60);
  CHECK(g.type == GestureType::None, "slow drag is ignored");
  // Long press fires once while held; the release after it is silent.
  std::vector<TouchSample> hold;
  for (int i = 0; i < 100; ++i) hold.push_back(down(80, 80));
  GestureDetector d2;
  int longs = 0, others = 0;
  uint32_t t = 0;
  for (const auto &s : hold) {
    const Gesture e = d2.update(s, t);
    if (e.type == GestureType::LongPress) ++longs;
    else if (e) ++others;
    t += 10;
  }
  for (int i = 0; i < 3; ++i) {
    const Gesture e = d2.update(up(), t);
    t += 10;
    if (e) ++others;
  }
  CHECK(longs == 1 && others == 0, "one long press, nothing on release (%d, %d)", longs, others);
}

void testGestureRobustness() {
  GestureDetector d;
  // A single dropped sample in the middle of a swipe must not end it early.
  std::vector<TouchSample> s;
  for (int i = 0; i < 5; ++i) s.push_back(down(250 - i * 20, 100));
  s.push_back(up()); // Glitch.
  for (int i = 5; i < 10; ++i) s.push_back(down(250 - i * 20, 100));
  s.push_back(up());
  s.push_back(up());
  Gesture g = run(d, s, 0, 15);
  CHECK(g.type == GestureType::SwipeLeft, "one-sample glitch does not cut the swipe (%d)", int(g.type));
  // Release without a touch, and idle samples, are quiet.
  GestureDetector e;
  CHECK(!e.update(up(), 0) && !e.update(up(), 10), "idle is quiet");
  CHECK(!e.touching(), "not touching");
  // A second touch right after the first works.
  g = run(e, {down(10, 10), up(), up()}, 100, 10);
  CHECK(g.type == GestureType::Tap, "tap");
  g = run(e, {down(300, 20), up(), up()}, 200, 10);
  CHECK(g.type == GestureType::Tap && g.x == 300, "second tap");
}

void testNavigation() {
  PageNavigator nav(8);
  CHECK(nav.index() == 0, "starts at page 0");
  nav.prev();
  CHECK(nav.index() == 7, "prev wraps to the last page");
  nav.next();
  nav.next();
  CHECK(nav.index() == 1, "next wraps to the first page");
  nav.set(99);
  CHECK(nav.index() == 7, "set clamps");
  nav.set(-4);
  CHECK(nav.index() == 0, "set clamps low");
  PageNavigator one(0);
  CHECK(one.count() == 1, "at least one page");

  CHECK(hitNavBar(20, 230) == NavButton::Prev, "prev button");
  CHECK(hitNavBar(100, 230) == NavButton::Action1, "action 1");
  CHECK(hitNavBar(200, 230) == NavButton::Action2, "action 2");
  CHECK(hitNavBar(300, 230) == NavButton::Next, "next button");
  CHECK(hitNavBar(160, 100) == NavButton::None, "content area is not a button");
  CHECK(hitNavBar(159, 230) == NavButton::None, "gap between buttons");
  CHECK(NavBarLayout::next.x + NavBarLayout::next.w == 320, "bar spans the screen");
}

// ------------------------------------------------------------ BatteryModel

void testBattery() {
  CHECK(battery::percentFromMillivolts(4200) == 100 && battery::percentFromMillivolts(4300) == 100, "full");
  CHECK(battery::percentFromMillivolts(3270) == 0 && battery::percentFromMillivolts(3000) == 0, "empty");
  CHECK(near(battery::percentFromMillivolts(3840), 50, 0.01f), "mid point");
  CHECK(near(battery::percentFromMillivolts(3835), 48.75f, 0.1f), "interpolates: %.2f", battery::percentFromMillivolts(3835));
  float last = -1;
  for (float mv = 3000; mv <= 4300; mv += 5) {
    const float p = battery::percentFromMillivolts(mv);
    CHECK(p >= last, "monotonic at %.0f mV", mv);
    last = p;
  }
  auto r = battery::evaluate(3840, false, true);
  CHECK(r.state == battery::State::OnBattery && near(r.percent, 50, 0.1f), "on battery");
  r = battery::evaluate(3920, true, true);
  CHECK(r.state == battery::State::Charging, "charging");
  CHECK(r.percent < battery::percentFromMillivolts(3920), "charging reading is corrected downwards");
  r = battery::evaluate(4180, false, true);
  CHECK(r.state == battery::State::Full, "idle charger at the top: full");
  r = battery::evaluate(4180, false, false);
  CHECK(r.state == battery::State::OnBattery, "no charge line (Rev 1): voltage only");
  r = battery::evaluate(3920, true, false);
  CHECK(r.state == battery::State::OnBattery, "charge line ignored when not fitted");
  r = battery::evaluate(900, false, true);
  CHECK(r.state == battery::State::NoBattery && r.percent == 0, "no battery");
  float v[5] = {3900, 3905, 4400, 3895, 3902}; // One spike.
  CHECK(near(battery::medianMillivolts(v, 5), 3902, 0.01f), "median rejects the spike");
  float one[1] = {1234};
  CHECK(battery::medianMillivolts(one, 1) == 1234 && battery::medianMillivolts(one, 0) == 0, "median edge cases");
}

// --------------------------------------------------------------- LogFormat

void testLogNames() {
  char path[32];
  logfmt::sessionPath(7, path, sizeof path);
  CHECK(std::strcmp(path, "/LOGS/S0007.CSV") == 0, "path %s", path);
  logfmt::sessionPath(12345, path, sizeof path);
  CHECK(std::strcmp(path, "/LOGS/S2345.CSV") == 0, "wraps to four digits: %s", path);
  unsigned n = 0;
  CHECK(logfmt::parseSessionNumber("S0042.CSV", n) && n == 42, "parse");
  CHECK(logfmt::parseSessionNumber("/LOGS/s0007.csv", n) && n == 7, "parse with directory, lower case");
  CHECK(!logfmt::parseSessionNumber("S42.CSV", n), "needs four digits");
  CHECK(!logfmt::parseSessionNumber("S0042.TXT", n), "needs .CSV");
  CHECK(!logfmt::parseSessionNumber("X0042.CSV", n), "needs S prefix");
  CHECK(!logfmt::parseSessionNumber("S00A2.CSV", n), "digits only");
  CHECK(!logfmt::parseSessionNumber(nullptr, n), "null");
  CHECK(logfmt::nextSession(0) == 1 && logfmt::nextSession(41) == 42 && logfmt::nextSession(9999) == 1, "next");
}

void testLogLines() {
  char line[96];
  std::size_t n = logfmt::formatSample(line, sizeof line, 1234567, 0.1234f, -0.5f, 9.80665f);
  CHECK(std::strcmp(line, "1234567,0.123,-0.500,9.807\n") == 0 && n == std::strlen(line), "sample line: %s", line);
  logfmt::formatSample(line, sizeof line, 0, -0.0001f, 0.0f, -12.3456f);
  CHECK(std::strcmp(line, "0,0.000,0.000,-12.346\n") == 0, "tiny negatives print without a minus sign: %s", line);
  logfmt::formatSample(line, sizeof line, 4294967295u, 78.4532f, -78.4532f, 0.0005f);
  CHECK(std::strcmp(line, "4294967295,78.453,-78.453,0.001\n") == 0, "extremes: %s", line);
  logfmt::formatSample(line, sizeof line, 1, 1e9f, -1e9f, NAN);
  CHECK(std::strlen(line) < 40 && line[std::strlen(line) - 1] == '\n', "absurd values stay bounded: %s", line);
  CHECK(logfmt::formatSample(line, 10, 1, 0, 0, 0) == 0, "too small a buffer writes nothing");

  logfmt::Header h;
  h.session = 12;
  h.uptimeMs = 345678;
  h.sampleRateHz = 500;
  h.rangeG = 8;
  logfmt::setTag(h, "12a3-4");
  CHECK(std::strcmp(h.tag, "1234") == 0, "tag keeps digits only: '%s'", h.tag);
  logfmt::setTag(h, "1234567890123");
  CHECK(std::strlen(h.tag) == logfmt::kMaxTagLen, "tag length is bounded");
  logfmt::setTag(h, "4711");
  char text[512];
  const std::size_t len = logfmt::formatHeader(text, sizeof text, h);
  CHECK(len == std::strlen(text), "header length");
  CHECK(std::strstr(text, "# session: 12\n") && std::strstr(text, "# tag: 4711\n") &&
            std::strstr(text, "# sample_rate_hz: 500.0\n") && std::strstr(text, "# columns: t_us,ax_m_s2"),
        "header content:\n%s", text);
  // Every header line is a comment so CSV readers can skip it.
  for (const char *p = text; *p; p = std::strchr(p, '\n') + 1) CHECK(*p == '#', "comment line");
  char ev[96];
  logfmt::formatBatteryEvent(ev, sizeof ev, 5000000, 3871.4f, true);
  CHECK(std::strcmp(ev, "# battery t_us=5000000 mV=3871 charging=1\n") == 0, "battery event: %s", ev);
}

// ------------------------------------------------------------------ Keypad

void testKeypad() {
  // Every key's centre hits that key; points in the gaps and outside hit nothing.
  int seen[12] = {};
  for (int row = 0; row < KeypadLayout::kRows; ++row)
    for (int col = 0; col < KeypadLayout::kCols; ++col) {
      const Rect r = KeypadLayout::rect(col, row);
      const Key k = KeypadLayout::hit(r.x + r.w / 2, r.y + r.h / 2);
      CHECK(k == KeypadLayout::keyAt(col, row), "key at %d,%d", col, row);
      if (int(k) >= 0) ++seen[int(k)];
    }
  for (int i = 0; i < 12; ++i) CHECK(seen[i] == 1, "key %d appears exactly once", i);
  CHECK(KeypadLayout::hit(KeypadLayout::kX + KeypadLayout::kKeyW + 1, KeypadLayout::kY + 5) == Key::None, "gap between keys");
  CHECK(KeypadLayout::hit(5, 5) == Key::None && KeypadLayout::hit(319, 239) == Key::None, "outside");
  CHECK(KeypadLayout::keyAt(0, 0) == Key::D1 && KeypadLayout::keyAt(2, 2) == Key::D9 &&
            KeypadLayout::keyAt(1, 3) == Key::D0 && KeypadLayout::keyAt(0, 3) == Key::Backspace &&
            KeypadLayout::keyAt(2, 3) == Key::Ok, "phone-style layout");
  const Rect last = KeypadLayout::rect(2, 3);
  CHECK(last.y + last.h <= NavBarLayout::kTop - 12, "keypad stays above the notice line and bar");
  CHECK(last.x + last.w <= 320, "keypad fits the width");

  TagEntry tag;
  tag.press(Key::D4);
  tag.press(Key::D7);
  tag.press(Key::D1);
  tag.press(Key::D1);
  CHECK(std::strcmp(tag.digits(), "4711") == 0 && tag.length() == 4, "typed digits");
  tag.press(Key::Backspace);
  CHECK(std::strcmp(tag.digits(), "471") == 0, "backspace");
  for (int i = 0; i < 20; ++i) tag.press(Key::D9);
  CHECK(tag.length() == TagEntry::kMax, "length is capped");
  CHECK(tag.press(Key::Ok), "Ok finishes");
  for (int i = 0; i < 20; ++i) tag.press(Key::Backspace);
  CHECK(tag.length() == 0 && tag.digits()[0] == '\0', "backspace on empty is safe");
  tag.set("12ab3");
  CHECK(std::strcmp(tag.digits(), "123") == 0, "set keeps digits only");
  tag.set(nullptr);
  CHECK(tag.length() == 0, "set(null) clears");
}

} // namespace

int main() {
  testQmiDecode();
  testClockUniform();
  testClockDriftAndGaps();
  testTouchMapping();
  testGestures();
  testGestureRobustness();
  testNavigation();
  testBattery();
  testLogNames();
  testLogLines();
  testKeypad();
  if (failures) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all port logic checks passed\n");
  return 0;
}
