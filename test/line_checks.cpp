// Host checks of line::Tracker on a simulated roller line:
//   g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc
// The firmware samples at 200 Hz (LINE_SAMPLE_HZ, platformio.ini); that is
// the rate these checks are tuned for.
//
// The main line: 8 tables, gaps along X and Y on the floor, an RPS_seat
// table (table 2: up and down, with a slight tilt) and two turntables
// (table 3 turns left by 90 deg, table 5 right), as its config.csv says.
// It runs with and without the structure shaking.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "LineTracker.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
// Column `i` (from 0) of a log row as a number; NAN when empty.
float column(const std::string &csv, int i) {
  size_t at = 0;
  for (int k = 0; k < i && at != std::string::npos; ++k) at = csv.find(',', at) + 1;
  if (at == std::string::npos || at >= csv.size() || csv[at] == ',') return NAN;
  return std::strtof(csv.c_str() + at, nullptr);
}
// The measured turn (measured_angle_deg) is `deg` within 2 deg: as measured, not snapped.
bool angleNear(const std::string &csv, float deg) { return std::fabs(column(csv, 8) - deg) < 2; }
constexpr int hz = LINE_SAMPLE_HZ;
constexpr float step = 1.0f / hz;
constexpr float deg = 3.14159265f / 180;
constexpr float gaps[] = {6.0f, 7.0f, 5.5f, 8.0f, 6.5f, 7.0f, 6.0f};
constexpr float dirs[] = {0, 0, 90, 90, 0, 0, 0}; // Floor direction of each gap, deg.

struct V { float x, y, z; };
V add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V mul(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V unit(V a) { return mul(a, 1 / std::sqrt(dot(a, a))); }
// Rodrigues: v turned by angle about unit axis k.
V turn(V v, V k, float a) {
  return add(add(mul(v, std::cos(a)), mul(cross(k, v), std::sin(a))), mul(k, dot(k, v) * (1 - std::cos(a))));
}

// Access points around the line; RSSI falls with distance, plus noise.
struct World {
  float apX[12], apY[12];
  uint32_t seed = 3;
  float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (2.0f / 16777216.0f) - 1.0f; }
  World() { for (int i = 0; i < 12; ++i) { apX[i] = -5 + i * 3.0f; apY[i] = -4 + (i % 4) * 5.0f; } }
  line::Fingerprint scan(float x, float y) {
    line::Fingerprint f;
    for (int i = 0; i < 12; ++i) {
      const float d = std::sqrt((x - apX[i]) * (x - apX[i]) + (y - apY[i]) * (y - apY[i]) + 9);
      const float rssi = -35 - 25 * std::log10(d) + 2.5f * rnd();
      if (rssi < -88) continue;
      line::Ap &a = f.ap[f.count++];
      for (int b = 0; b < 6; ++b) a.bssid[b] = uint8_t(i * 7 + b);
      a.rssi = rssi;
    }
    return f;
  }
};

// The sensor on the body. Its mount is a little tilted (gravity is not on
// its Z) and it starts turned 30 deg to the floor's X. The body's pose is
// kept as the sensor axes in floor coordinates and turned by the angular
// rate, so gravity, accelerations and gyro always agree.
struct Part {
  line::Tracker &t;
  World &w;
  uint32_t us = 0, seed = 11;
  float x = 0, y = 0;     // True position on the floor, m.
  float shake = 0;        // Shaking of the structure the tables hang from (1 = strong).
  V g0{0.3f, -0.2f, 9.9f}; // Gravity in the sensor axes at the start.
  V bx{}, by{}, bz{};      // Sensor axes in floor coordinates (Z up).
  bool posed = false;
  float elevatorV = 0.3f; // Lift shaft speed, m/s.
  float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (2.0f / 16777216.0f) - 1.0f; }
  // Horizontal axes of the mount, in the sensor axes.
  V up0() const { return unit(g0); }
  V h1() const { V a = add(V{1, 0, 0}, mul(up0(), -up0().x)); return unit(a); }
  V h2() const { return cross(up0(), h1()); }
  void pose() {
    const float psi = 30 * deg;
    const V H1{std::cos(psi), std::sin(psi), 0}, H2{-std::sin(psi), std::cos(psi), 0}, U{0, 0, 1};
    const auto floorOf = [&](V e) { return add(add(mul(H1, dot(e, h1())), mul(H2, dot(e, h2()))), mul(U, dot(e, up0()))); };
    bx = floorOf({1, 0, 0}); by = floorOf({0, 1, 0}); bz = floorOf({0, 0, 1});
    posed = true;
  }
  void copyPose(const Part &o) { bx = o.bx; by = o.by; bz = o.bz; posed = o.posed; }
  V toSensor(V f) const { return {dot(f, bx), dot(f, by), dot(f, bz)}; }
  V toFloor(V b) const { return add(add(mul(bx, b.x), mul(by, b.y)), mul(bz, b.z)); }
  // One sample: floor acceleration (ax, ay), vertical acceleration, turn
  // rate about the floor's vertical, tilt rate about the mount's h1 axis,
  // rolling vibration.
  void sample(float ax, float ay, float av = 0, float yawRate = 0, float tiltRate = 0, float vib = 0) {
    if (!posed) pose();
    us += 1000000 / hz;
    const float ts = us * 1e-6f * 2 * 3.14159f;
    const float rock = shake * 0.08f * std::sin(ts * 7 + 0.5f);
    // Angular rate in floor coordinates; turn the sensor axes by it.
    const V rate = add(add(V{0, 0, yawRate}, mul(toFloor(h1()), tiltRate)), toFloor(V{rock, 0, 0.5f * rock}));
    const float r = std::sqrt(dot(rate, rate));
    if (r > 1e-9f) {
      const V k = mul(rate, 1 / r);
      bx = turn(bx, k, r * step); by = turn(by, k, r * step); bz = turn(bz, k, r * step);
    }
    const V a0 = toSensor(V{ax, ay, std::sqrt(dot(g0, g0)) + av}); // Gravity reaction plus motion.
    const V roll{vib * std::sin(ts * 15), vib * std::sin(ts * 40), vib * 0.6f * std::sin(ts * 23)};
    const V structure{shake * (0.8f * std::sin(ts * 7) + 0.15f * std::sin(ts * 0.9f)), shake * 0.5f * std::sin(ts * 11 + 1),
                      shake * 1.0f * std::sin(ts * 5 + 2)};
    const V a = add(add(a0, roll), structure);
    V g = toSensor(rate);
    const float clip = 500 * deg; // The MPU6050's +-500 deg/s range.
    g = {std::fmax(-clip, std::fmin(clip, g.x)), std::fmax(-clip, std::fmin(clip, g.y)), std::fmax(-clip, std::fmin(clip, g.z))};
    t.add({a.x + 0.07f * rnd(), a.y + 0.07f * rnd(), a.z + 0.07f * rnd()},
          {0.04f + g.x + 0.003f * rnd(), -0.03f + g.y + 0.003f * rnd(), 0.02f + g.z + 0.003f * rnd()}, us);
    if (t.scanWanted()) { t.scanStarted(); t.applyScan(w.scan(x, y)); }
  }
  void stand(float s) { for (int i = 0; i < int(s * hz); ++i) sample(0, 0); }
  // Ramp up to 0.6 m/s (jerk-limited, 1.5 s), roll, brake the same way;
  // along `dirDeg` on the floor.
  void ride(float d, float dirDeg, bool sensorGap = false) {
    const float vmax = 0.6f, ta = 1.5f, c = std::cos(dirDeg * deg), s = std::sin(dirDeg * deg);
    const float cruise = (d - vmax * ta) / vmax, total = 2 * ta + cruise;
    float v = 0;
    for (float tt = 0; tt < total; tt += step) {
      float a = 0;
      const auto bell = [&](float u) { return vmax / ta * 6 * u * (1 - u); };
      if (tt < ta) a = bell(tt / ta);
      else if (tt > ta + cruise) a = -bell((tt - ta - cruise) / ta);
      v += a * step;
      x += c * v * step;
      y += s * v * step;
      if (sensorGap && tt >= total / 2 && tt < total / 2 + step) us += 300000; // Samples lost mid-ride.
      sample(a * c, a * s, 0, 0, 0, 0.6f * std::fabs(v) / vmax);
    }
  }
  // Turntable: turn by `angDeg` in 10 s (smooth), then settle.
  // `armM` > 0: the sensor sits that far from the table's axis (beside the
  // body's way, on its left), so it swings along an arc while turning.
  void turntable(float angDeg, float armM = 0) {
    const float T = 10;
    const float phi0 = -90 * deg; // Direction from the axis to the sensor: the axis is on the body's left.
    const float cx = x - armM * std::cos(phi0), cy = y - armM * std::sin(phi0);
    float th = 0, prevRate = 0;
    for (float tt = 0; tt < T; tt += step) {
      const float s = std::sin(3.14159f * tt / T);
      const float rate = angDeg * deg / T * 2 * s * s, acc = (rate - prevRate) / step;
      prevRate = rate;
      th += rate * step;
      const float c = std::cos(phi0 + th), sn = std::sin(phi0 + th);
      // Arc: tangential acc * arm along (-sin, cos), centripetal rate^2 * arm toward the axis.
      const float ax = armM * (acc * -sn - rate * rate * c), ay = armM * (acc * c - rate * rate * sn);
      x = cx + armM * c; y = cy + armM * sn;
      sample(ax, ay, 0, rate);
    }
    stand(3);
  }
  // Carried by hand to (tx, ty) in `secs`: walking, rocking +-12 deg,
  // turned 60 deg and back, bobbing up and down; set down gently.
  // Turntable that starts and stops abruptly (rate steps, 0.3 s ramps).
  void turntableAbrupt(float angDeg, float armM) {
    const float T = 6, ramp = 0.3f, w = angDeg * deg / (T - ramp);
    const float phi0 = -90 * deg, cx = x - armM * std::cos(phi0), cy = y - armM * std::sin(phi0);
    float th = 0, prevRate = 0;
    for (float tt = 0; tt < T; tt += step) {
      const float rate = tt < ramp ? w * tt / ramp : tt > T - ramp ? w * (T - tt) / ramp : w, acc = (rate - prevRate) / step;
      prevRate = rate; th += rate * step;
      const float c = std::cos(phi0 + th), sn = std::sin(phi0 + th);
      x = cx + armM * c; y = cy + armM * sn;
      sample(armM * (acc * -sn - rate * rate * c), armM * (acc * c - rate * rate * sn), 0, rate);
    }
  }
  // `knock`: knocked once on the way (an 860 deg/s flick, past the gyro's
  // range, and slowly back), which spoils the attitude the gyro keeps.
  void carry(float tx, float ty, float secs, bool knock = false) {
    const float x0 = x, y0 = y;
    float vx = 0, vy = 0;
    for (float tt = 0; tt < secs; tt += step) {
      const float u = tt / secs, k = 30 * u * u * (1 - u) * (1 - u) / secs; // Min-jerk speed shape.
      const float nvx = (tx - x0) * k, nvy = (ty - y0) * k;
      const float ax = (nvx - vx) / step, ay = (nvy - vy) / step;
      vx = nvx; vy = nvy;
      x += vx * step; y += vy * step;
      const float sway = 12 * deg * 2 * 3.14159f * 0.7f * std::cos(2 * 3.14159f * 0.7f * tt) * std::sin(3.14159f * u);
      const float yaw = 60 * deg / secs * 3.14159f * std::sin(2 * 3.14159f * u); // Out and back.
      float flick = 0;
      if (knock && u > 0.6f && u < 0.6f + 0.06f / secs) flick = 15;                            // 0.06 s at 15 rad/s ...
      else if (knock && u >= 0.6f + 0.06f / secs && u < 0.6f + 0.56f / secs) flick = -15 * 0.06f / 0.5f; // ... back in 0.5 s.
      sample(ax, ay, 1.5f * std::sin(2 * 3.14159f * 1.8f * tt) * std::sin(3.14159f * u), yaw, sway + flick);
    }
    x = tx; y = ty;
  }
  // Lift shaft (elevator): `h` metres up (+) or down (-), ramp to 0.3 m/s
  // in 1.5 s, steady, brake the same way. Level all the way.
  void elevator(float h) {
    const float ta = 1.5f, sign = h > 0 ? 1.0f : -1.0f;
    const float vmax = std::fmin(elevatorV, std::fabs(h) / ta); // A short move never reaches full speed.
    const float cruise = (std::fabs(h) - vmax * ta) / vmax, total = 2 * ta + cruise;
    for (float tt = 0; tt < total; tt += step) {
      const auto bell = [&](float u) { return vmax / ta * 6 * u * (1 - u); };
      float a = 0;
      if (tt < ta) a = bell(tt / ta);
      else if (tt > ta + cruise) a = -bell((tt - ta - cruise) / ta);
      sample(0, 0, sign * a, 0, 0, 0.3f);
    }
  }
  // Lifting table: up 0.4 m in 4 s tilting 1.5 deg, wait, down again.
  void lift() {
    const float T = 4, h = 0.4f, tiltTo = 1.5f * deg;
    for (int dirn : {1, -1}) {
      for (float tt = 0; tt < T; tt += step) {
        const float u = tt / T;
        sample(0, 0, dirn * h / (T * T) * (60 * u - 180 * u * u + 120 * u * u * u), 0,
               dirn * tiltTo / T * 30 * u * u * (1 - u) * (1 - u));
      }
      stand(8);
    }
  }
};

// The main line as config.csv. Directions are the sensor's, and the body turns
// with it: every gap is along the body's X+ (the mount is turned 30 deg to the
// floor, well within what tells forward from back).
line::StationConfig mainLine() {
  line::StationConfig cfg;
  const bool ok = line::parseStationConfig(
    "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
    "T1,X+,0,true,none,,\n"
    "T2,X+,6,true,RPS_seat,Z,0.4\n"
    "T3,X+,7,true,rotation_z,Z,-90\n" // Turns left.
    "T4,X+,5.5,true,none,,\n"
    "T5,X+,8,true,rotation_z,Z,90\n"  // Turns right.
    "T6,X+,6.5,true,none,,\n"
    "T7,X+,7,true,none,,\n"
    "T8,X+,6,true,none,,\n", cfg);
  check(ok, "setup: the main line's config.csv parses");
  return cfg;
}

void suite(float shake) {
  std::printf("--- structure shaking %s\n", shake > 0 ? "on" : "off");
  World world;
  auto tracker = std::make_unique<line::Tracker>();
  line::Tracker &t = *tracker;
  const line::StationConfig cfg = mainLine();
  t.applyStationConfig(cfg);
  Part part{t, world};
  part.shake = shake;

  part.stand(10); // 1.5 s first estimates + 8 s learning the shaking.
  check(t.phase() == line::Tracker::Phase::Parked, "standing part is parked");
  part.stand(120);
  std::printf("      shaking %.3f m/s2, ride threshold %.3f m/s, activity %.3f m/s\n", t.shaking(),
              t.startThreshold(), t.activity());
  check(t.phase() == line::Tracker::Phase::Parked && t.transitCount() == 0, "120 s standing: no ride");
  t.home();
  part.stand(1);
  check(t.known() && t.station() == 0 && t.learned.fp[0].count > 0, "home: table 1, fingerprint learned");

  // First pass along X and Y, with a lifting table and two turntables.
  bool allRight = true, liftQuiet = true;
  float worst = 0, worstLevel = 0;
  for (int s = 1; s < 8; ++s) {
    part.ride(gaps[s - 1], dirs[s - 1]);
    part.stand(18);
    const auto &tr = t.transit(t.transitCount() - 1);
    worst = std::fmax(worst, std::fabs(tr.distance - gaps[s - 1]));
    // The floor's 0 is the body's heading at table 1 (30 deg in this world).
    std::printf("      ride %d: %.2f m (true %.2f) dir %.0f (true %.0f) at table %d\n", s, tr.distance, gaps[s - 1],
                tr.floorDeg, dirs[s - 1] - 30, t.station() + 1);
    if (t.station() != s) allRight = false;
    if (s == 1) { // RPS_seat table at table 2: up and down again.
      const int before = t.transitCount();
      part.lift();
      part.stand(5);
      liftQuiet = t.transitCount() == before && t.station() == 1 && t.phase() == line::Tracker::Phase::Parked;
      worstLevel = std::fmax(worstLevel, t.startThreshold());
    }
    if (s == 2) {
      part.turntable(90);
      std::printf("      turntable: turn %.1f deg, heading %.1f deg\n", t.lastTurn() / deg, t.heading() / deg);
      check(std::fabs(t.lastTurn() / deg - 90) < 1, "turntable +90 deg recognised");
      worstLevel = std::fmax(worstLevel, t.startThreshold());
    }
    if (s == 4) {
      part.turntable(-90);
      check(std::fabs(t.lastTurn() / deg + 90) < 1, "turntable -90 deg recognised");
      worstLevel = std::fmax(worstLevel, t.startThreshold());
    }
    part.stand(5);
  }
  std::printf("      worst distance error %.2f m, highest ride threshold after lift/turns %.3f m/s\n", worst, worstLevel);
  check(allRight, "first pass counts every table (X, Y, after turns)");
  check(worst < 0.6f, "transit distances within 0.6 m");
  check(liftQuiet, "RPS_seat table: no ride, same table");
  check(worstLevel <= line::Tracker::minStartV + 1e-3f, "lift and turns do not raise the threshold above its floor");
  int learnedFp = 0;
  for (int s = 0; s < 8; ++s) learnedFp += t.learned.fp[s].count > 0;
  check(learnedFp == 8, "every table has a fingerprint");
  const auto &last = t.transit(t.transitCount() - 1);
  check(last.rms[0] > 0.1f && last.bins > 50 && last.ramp > 0.3f, "transit vibration recorded");
  bool anyCarried = false;
  for (int i = 0; i < t.transitCount(); ++i) anyCarried |= t.transit(i).carried;
  check(!anyCarried, "no roller ride taken for a carry");

  // Carried by hand from table 8 to table 3: Wi-Fi finds it.
  float tableX[8] = {}, tableY[8] = {};
  for (int s = 1; s < 8; ++s) {
    tableX[s] = tableX[s - 1] + gaps[s - 1] * std::cos(dirs[s - 1] * deg);
    tableY[s] = tableY[s - 1] + gaps[s - 1] * std::sin(dirs[s - 1] * deg);
  }
  part.carry(tableX[2], tableY[2], 19.5f);
  check(t.carrying(), "being carried is recognised right away");
  part.carry(tableX[2], tableY[2], 0.5f);
  part.stand(4);
  part.stand(18);
  std::printf("      after the carry: table %d known %d (true 3), last transit carried %d to %d\n", t.station() + 1,
              t.known(), t.transit(t.transitCount() - 1).carried, t.transit(t.transitCount() - 1).to + 1);
  check(t.known() && t.station() == 2, "after the carry Wi-Fi finds table 3");
  check(t.transit(t.transitCount() - 1).carried && t.transit(t.transitCount() - 1).to == 2, "the carry is listed, to table 3");
  // Carried back to table 8 (the way the rest of the test expects it).
  part.carry(tableX[7], tableY[7], 20);
  part.stand(22);
  std::printf("      carried back: after one scan known %d\n", t.known());
  part.stand(120); // A whole stop: scans every 45 s, averaged.
  check(t.known() && t.station() == 7, "carried back: Wi-Fi finds table 8 (close neighbour of 7)");

  // Back to table 1 the other way (the body keeps its heading).
  for (int s = 6; s >= 0; --s) { part.ride(gaps[s], dirs[s] + 180); part.stand(20); }
  check(t.station() == 0, "riding back counts down to table 1");

  // Second pass with lost samples mid-ride: Wi-Fi finds the table.
  part.ride(gaps[0], dirs[0]); part.stand(20);
  part.ride(gaps[1], dirs[1]); part.stand(20);
  part.ride(gaps[2], dirs[2], true);
  check(!t.known(), "lost samples make the table unknown");
  part.stand(20);
  check(t.known() && t.station() == 3, "Wi-Fi finds table 4 after the gap");

  // Power cycle: the fingerprints are kept; Wi-Fi finds the table and its heading.
  auto fresh = std::make_unique<line::Tracker>();
  fresh->applyStationConfig(cfg);
  fresh->learned = t.learned;
  Part again{*fresh, world, part.us};
  again.x = part.x; again.y = part.y; again.copyPose(part); again.shake = shake;
  again.stand(26); // Learning the shaking, then 15 s to the fingerprint.
  check(fresh->known() && fresh->station() == 3, "after a power cycle Wi-Fi finds table 4");
  again.ride(gaps[3], dirs[3]);
  again.stand(20);
  check(fresh->station() == 4, "and the next ride (along Y) counts on from there");

  // A short nudge keeps the table.
  again.ride(1.2f, 0);
  again.stand(20);
  check(fresh->station() == 4, "a 1.2 m nudge keeps the table");
}
} // namespace

int main() {
  suite(0);
  suite(1);

  // --- Per-station vibration is recorded on arrival ---
  {
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    line::StationConfig cfg;
    line::parseStationConfig("A,X+,0,true,none,,\nB,X+,6,true,none,,\n", cfg);
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    part.stand(120);
    t.home();
    part.stand(1);
    check(t.stationVib(0, 0) == 0 && t.stationVib(1, 0) == 0, "no vibration recorded before any ride");
    part.ride(gaps[0], dirs[0]); // Real `Part::ride` helper, used by the scenarios above.
    part.stand(18);
    check(t.stationVib(1, 0) >= 0, "arriving at a table records a non-negative X vibration RMS");
  }

  // --- The line comes from config.csv; a ride never changes it ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "ST01,X+,10,true,none,,\n"
      "ST02,X+,10,true,none,,\n"; // CSV says 10 m; the simulated ride below is only 6 m.
    check(line::parseStationConfig(text, cfg), "setup: config parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    check(t.configAuthoritative(), "tracker reports CSV-authoritative after applyStationConfig");
    check(std::fabs(t.gap(0) - 10.0f) < 1e-3f, "gap(0) comes from CSV, not default");
    Part part{t, world};
    part.stand(10); // Settle into Parked.
    t.home();
    part.stand(1);
    part.ride(gaps[0], dirs[0]); // Real `Part::ride` helper; the scenarios' 6 m synthetic gap.
    part.stand(18);
    check(std::fabs(t.gap(0) - 10.0f) < 1e-3f, "gap(0) is unchanged by the actual 6 m ride: nothing is learned");
  }

  // --- RotationZ technological logging fires from the turn event itself
  // (Tracker::endTurn()), not from finish(): a turn recognised while parked
  // never reaches finish() (only called from Phase::Moving). ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "ST01,X+,20,true,rotation_z,Z,-90\n"
      "ST02,X+,20,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: rotation_z station config parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    t.home();
    part.stand(1);
    check(t.station() == 0 && t.known(), "setup: at table 1 (rotation_z configured there)");
    part.turntable(90); // Parked the whole time: turns +90 deg in 10 s, then settles.
    check(t.newDistanceRow(), "rotation_z: a parked turn produced a distance-log row");
    const std::string row = t.distanceRowCsv();
    std::printf("      rotation_z row: %s", row.c_str());
    check(row.find("technological") != std::string::npos && row.find("rotation_z") != std::string::npos,
          "rotation_z: row is technological, motion_type rotation_z");
    check(angleNear(row, -90),
          "rotation_z: measured_angle_deg is ~-90 deg (a left turn about Z as shown, down), not 0");
  }

  // --- A ride that settles back at the same table ("nudge", d >=
  // minTransitM but under nudgeShare * gap) is a maneuver (lift/z_transfer),
  // not a same-table "transport": astra audit finding 4. Gap here is 20 m,
  // nudgeShare is 0.35, so a 2 m ride (>= minTransitM=1.5, < 7 m) resolves
  // to the same table via targetFor() and must log "technological". ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "ST01,X+,20,true,z_transfer,Z,0.3\n"
      "ST02,X+,20,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: z_transfer station config parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    t.home();
    part.stand(1);
    const int before = t.transitCount();
    part.ride(2.0f, 0); // >= minTransitM, < nudgeShare * 20 m: a nudge, same table.
    part.stand(18);
    check(t.station() == 0, "a nudge-sized ride stays at the same table");
    check(t.newDistanceRow(), "a maneuver at the table produced a distance-log row");
    const std::string row = t.distanceRowCsv();
    std::printf("      nudge/maneuver row: %s", row.c_str());
    check(std::string(t.distanceRowFile()) == "ST01_tech", "a maneuver is logged to ST01_tech");
    check(row.find("technological") != std::string::npos && row.find("z_transfer") != std::string::npos,
          "nudge is classified as technological motion (z_transfer), not a transport ride");
    check(row.find("transport") == std::string::npos,
          "nudge is NOT logged as a same-table transport");
    check(t.transitCount() == before,
          "a maneuver at the table does not add a transit record (that's for real rides/carries)");
  }

  // --- wifi_scan in config.csv: only those tables are scanned. ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "B1,X+,0,true,none,,\n"
      "B2,X+,6.5,false,none,,\n"
      "B3,X+,6.5,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: wifi_scan line parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    t.home();
    part.stand(1);
    part.ride(6.5f, 30); part.stand(22);
    part.ride(6.5f, 30); part.stand(22);
    check(t.station() == 2, "wifi_scan: rode to B3");
    check(std::string(t.distanceRowFile()) == "B2-B3", "the last ride is logged to B2-B3");
    check(t.learned.fp[0].count > 0 && t.learned.fp[1].count == 0 && t.learned.fp[2].count > 0,
          "wifi_scan: fingerprints at B1 and B3 only, none at B2");
  }

  // --- Carried and set down, with config.csv: 3 s still, a Wi-Fi scan, and
  // the position is the best match at once. ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "B1,X+,0,true,none,,\n"
      "B2,X+,8,true,none,,\n"
      "B3,X+,8,true,none,,\n"
      "B4,X+,8,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: carry line parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    t.home();
    part.stand(1);
    for (int s = 0; s < 3; ++s) { part.ride(8, 30); part.stand(22); }
    check(t.station() == 3, "setup: rode to B4, every table has a fingerprint");
    const float c30 = std::cos(30 * deg), s30 = std::sin(30 * deg);
    part.carry(8 * c30, 8 * s30, 15); // By hand back to B2.
    part.stand(5);                     // Set down: 3 s still, then the scan.
    std::printf("      carried to B2 with config.csv: known %d at %d\n", t.known(), t.station() + 1);
    check(t.known() && t.station() == 1, "5 s after setting it down Wi-Fi has put it at B2");
    check(t.transit(t.transitCount() - 1).carried && t.transit(t.transitCount() - 1).to == 1, "the carry is listed, to B2");
  }

  // --- Turntable as a transition (RZ+ / RZ- in config.csv): turning while
  // standing moves to the next position; a turntable at a station
  // (rotation_z) or a small turn does not. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- turntable transitions, structure shaking %s\n", shake > 0 ? "on" : "off");
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "B1,X+,0,true,none,,\n"
      "B2,X+,6.5,false,none,,\n"
      "B2r,RZ-,90,false,none,,\n" // RZ- turns left.
      "B3,X+,6.5,false,none,,\n" // The body's X+ after turning left: the floor's Y.
      "B3r,RZ+,90,true,none,,\n"; // RZ+ turns right.
    check(line::parseStationConfig(text, cfg), "setup: RZ- (left) / RZ+ (right) parse");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    check(t.turntable(1) == 1 && t.turntable(3) == -1 && t.turntable(0) == 0 && !t.floorGap(1),
          "turntables 2-3 (left) and 4-5 (right) taken from the config");
    check(t.floorGap(2) && std::fabs(t.shownDir(2) - 90 * deg) < 1e-4f,
          "X+ after a left turn: the sensor's X+, drawn at the floor's +90 deg");
    Part part{t, world};
    part.shake = shake;
    part.stand(40); // Learning the shaking.
    t.home();
    part.stand(1);
    part.ride(6.5f, 30); part.stand(22);
    check(t.station() == 1, "ride to B2");
    part.turntable(30); part.stand(5);
    check(t.station() == 1, "a 30 deg turn (under half of 90) keeps B2");
    part.turntable(-30); part.stand(5);
    part.turntable(90); part.stand(5);
    check(t.station() == 2 && std::string(t.distanceRowFile()) == "B2-B2r", "turned 90 deg left: B2r, logged to B2-B2r");
    std::printf("      turntable row: %s", t.distanceRowCsv());
    check(std::string(t.distanceRowCsv()).find(",RZ,0.000,") != std::string::npos && angleNear(t.distanceRowCsv(), -90) &&
          column(t.distanceRowCsv(), 11) == -90, "the log row carries the measured angle and the declared -90");
    part.stand(20);
    part.ride(6.5f, 30 + 90); part.stand(22); // After the turn the line goes on along the floor's Y.
    check(t.station() == 3, "ride on (the body's X+) after the turntable: B3");
    part.turntable(-90); part.stand(20);
    check(t.station() == 4, "turned 90 deg right: B3r");
    part.turntable(90); part.stand(20);
    check(t.station() == 3, "turned back left: B3 again");
  }

  // --- A turntable at a station (rotation_z): the same table, the body
  // turns with the sensor, then rides on along the sensor's X+ while the
  // line goes on turned on the floor. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- rotation_z station, structure shaking %s\n", shake > 0 ? "on" : "off");
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "B024,X+,0,true,none,,\n"
      "B025,X+,6.5,false,none,,\n"
      "B026,X+,6.5,false,rotation_z,Z,-90\n" // Turns left (about Z as shown, down).
      "B027,X+,6.5,false,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: rotation_z station line parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    check(std::fabs(t.shownDir(1)) < 1e-4f && std::fabs(t.shownDir(2) - 90 * deg) < 1e-4f,
          "drawn: one corner at B026, the line after it at +90 deg");
    Part part{t, world};
    part.shake = shake;
    part.stand(40);
    t.home();
    part.stand(1);
    part.ride(6.5f, 30); part.stand(22);
    part.ride(6.5f, 30); part.stand(22);
    check(t.station() == 2, "rode to B026");
    part.turntable(90); part.stand(20); // Left, on the table.
    check(t.station() == 2, "turned on B026: still B026");
    check(std::string(t.distanceRowFile()) == "B026_tech" &&
          angleNear(t.distanceRowCsv(), -90), "the turn is logged to B026_tech, -90 deg");
    part.ride(6.5f, 30 + 90); part.stand(22); // The sensor's X+, the floor's turned line.
    check(t.station() == 3, "rode on along the sensor's X+: B027");
    part.ride(6.5f, 30 + 90 + 180); part.stand(22);
    check(t.station() == 2, "and back: B026");
    part.turntable(-90); part.stand(20);
    part.ride(6.5f, 30 + 180); part.stand(22);
    check(t.station() == 1, "turned back and rode back: B025");
  }

  // --- The sensor off the turntable's axis: it swings along an arc, which
  // must not count as a ride; the turn is the station's own motion. ---
  for (float arm : {1.0f, 2.0f})
    for (float shake : {0.0f, 1.0f}) {
      std::printf("--- off-axis turntable, arm %.0f m, structure shaking %s\n", arm, shake > 0 ? "on" : "off");
      line::StationConfig cfg;
      const char *text =
        "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
        "B024,X+,0,true,none,,\n"
        "B025,X+,6.5,false,none,,\n"
        "B026,X+,6.5,false,rotation_z,Z,-90\n"
        "B027,X+,6.5,false,none,,\n";
      check(line::parseStationConfig(text, cfg), "setup: parses");
      World world;
      auto tracker = std::make_unique<line::Tracker>();
      line::Tracker &t = *tracker;
      t.applyStationConfig(cfg);
      Part part{t, world};
      part.shake = shake;
      part.stand(40);
      t.home();
      part.stand(1);
      part.ride(6.5f, 30); part.stand(22);
      part.ride(6.5f, 30); part.stand(22);
      const int rides = t.transitCount();
      part.turntable(90, arm); part.stand(20);
      std::printf("      after the turn: table %d known %d, transits %d -> %d, row file %s\n", t.station() + 1, t.known(),
                  rides, t.transitCount(), t.distanceRowFile());
      check(t.known() && t.station() == 2 && t.transitCount() == rides, "off-axis turn on B026: still B026, no ride");
      check(std::string(t.distanceRowFile()) == "B026_tech", "the turn is logged to B026_tech");
      part.ride(6.5f, 30 + 90); part.stand(22);
      check(t.station() == 3, "then the ride along the sensor's X+: B027");
      // An abrupt turn back and the ride out at once, without a stop.
      part.ride(6.5f, 30 + 90 + 180); part.stand(22);
      part.turntableAbrupt(-90, arm);
      part.ride(6.5f, 30 + 180); part.stand(22);
      check(t.station() == 1 && std::string(t.distanceRowFile()) == "B026-B025",
            "abrupt turn back and riding out at once: B025, the turn not counted as a ride");
    }

  // --- Carried with a knock on the way (past the gyro's range): the attitude
  // the gyro kept is off, yet the stop must still be found, the scan made
  // and the table found quickly. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- knocked carry, structure shaking %s\n", shake > 0 ? "on" : "off");
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "B1,X+,0,true,none,,\n"
      "B2,X+,8,true,none,,\n"
      "B3,X+,8,true,none,,\n"
      "B4,X+,8,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.shake = shake;
    part.stand(40);
    t.home();
    part.stand(1);
    for (int s = 0; s < 3; ++s) { part.ride(8, 30); part.stand(22); }
    const float c30 = std::cos(30 * deg), s30 = std::sin(30 * deg);
    part.carry(8 * c30, 8 * s30, 15, true);
    float found = -1;
    for (int k = 0; k < 40 && found < 0; ++k) { part.stand(0.5f); if (t.known() && t.phase() == line::Tracker::Phase::Parked) found = 0.5f * (k + 1); }
    std::printf("      knocked carry: found after %.1f s, table %d, activity %.3f / %.3f\n", found, t.station() + 1,
                t.activity(), t.startThreshold());
    check(found > 0 && found <= 8 && t.station() == 1, "knocked carry: B2 found within 8 s of setting it down");
    part.stand(30);
    check(t.phase() == line::Tracker::Phase::Parked && t.station() == 1, "and it stays parked at B2 (no endless ride)");
  }

  // --- Rides through several tables without stopping: one ride over two or
  // three gaps, counted from its distance. The steady roll in the middle is as calm as standing,
  // longer than `forcedStopS`; the ride must still end at its braking. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- through tables, structure shaking %s\n", shake > 0 ? "on" : "off");
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "AUF120,X+,0,true,none,,\n"
      "AUF130,X+,8,false,none,,\n"
      "AUF140,X+,8,false,none,,\n"
      "AUF150,X+,8,false,none,,\n"
      "AUF160,X+,8,false,none,,\n"
      "AUF170,X+,8,false,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: line parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.shake = shake;
    part.stand(10);
    part.stand(30);
    t.home();
    part.stand(1);
    part.ride(16, 30); part.stand(22);
    const auto &a = t.transit(t.transitCount() - 1);
    std::printf("      16 m ride: %.2f m, %d -> %d, carried %d\n", a.distance, a.from + 1, a.to + 1, a.carried);
    check(t.station() == 2 && t.transitCount() == 1 && !a.carried, "16 m through AUF130: AUF140, one ride");
    part.ride(24, 30); part.stand(22);
    const auto &b = t.transit(t.transitCount() - 1);
    std::printf("      24 m ride: %.2f m, %d -> %d, carried %d\n", b.distance, b.from + 1, b.to + 1, b.carried);
    check(t.station() == 5 && t.transitCount() == 2 && !b.carried, "24 m through AUF150 and AUF160: AUF170, one ride");
    part.ride(24, 210); part.stand(22);
    part.ride(16, 210); part.stand(22);
    check(t.station() == 0 && t.transitCount() == 4, "back 24 m and 16 m: AUF120");
  }

  // --- Lift shafts (Z- / Z+ in config.csv): an elevator ride moves to the
  // next stop, a lifting table or the structure bobbing does not. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- lift shafts, structure shaking %s\n", shake > 0 ? "on" : "off");
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "AUF120,X+,0,true,lift,Z-,0.5\n"
      "AUF130,X+,8,false,none,,\n"
      "AUF170,X+,8,false,none,,\n"
      "AUF170d,Z-,1,false,none,,\n"
      "AUF171,Y-,3,false,none,,\n"
      "AUF180,Y-,3,false,none,,\n"
      "AUF180u,Z+,1,false,none,,\n"
      "AUF181,X-,8,true,lift,Z-,0.5\n";
    check(line::parseStationConfig(text, cfg), "setup: line with two lift shafts parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    check(t.shaft(2) == -1 && t.shaft(5) == 1 && t.shaft(0) == 0 && !t.floorGap(2),
          "shafts 3-4 (down) and 6-7 (up) taken from the config");
    Part part{t, world};
    part.shake = shake;
    part.stand(10);
    t.home();
    part.stand(1);
    // The body's heading at table 1 is 30 deg on the simulated floor.
    const float X = 30, Ym = 30 - 90, Xm = 30 + 180;
    part.lift(); // Lifting table at AUF120 (lift): up, then down; the station stays.
    part.stand(5);
    check(t.station() == 0 && t.transitCount() == 0, "lifting table at AUF120: same table, no transit");
    part.ride(8, X); part.stand(22);
    part.ride(8, X); part.stand(22);
    check(t.station() == 2, "two rides along X: at AUF170 (next to the down shaft)");
    part.stand(120);
    check(t.station() == 2, "120 s standing next to the shaft: the structure bobbing moves nothing");
    part.elevator(0.25f); part.stand(22);
    check(t.station() == 2, "a 0.25 m jolt in the shaft (under 35% of 1 m) keeps the stop");
    part.elevator(-1); part.stand(22);
    const auto &down = t.transit(t.transitCount() - 1);
    std::printf("      down the shaft: %.2f m lift %d, at %d\n", down.distance, down.lift, t.station() + 1);
    check(t.station() == 3 && down.lift == -1 && std::fabs(down.distance - 1) < 0.2f,
          "elevator 1 m down: AUF170d, transit marked as a lift ride");
    check(std::string(t.distanceRowCsv()).find("transport") != std::string::npos, "lift ride logged as transport");
    part.ride(3, Ym); part.stand(22);
    part.ride(3, Ym); part.stand(22);
    check(t.station() == 5, "two rides along Y- from the lower stop: AUF180");
    part.elevator(1); part.stand(22);
    check(t.station() == 6, "elevator 1 m up: AUF180u");
    part.ride(8, Xm); part.stand(22);
    check(t.station() == 7, "ride along X- from the upper stop: AUF181");
    part.lift(); part.stand(5);
    check(t.station() == 7, "lifting table at AUF181: same table");
    // The tilt of the lifting table spoils the gyro offset for a while (it is
    // learned at the slow ends of the tilt); riding off within seconds can
    // then leave the ride without a calm stop. Not a lift-shaft matter.
    part.stand(30);
    // And all the way back.
    part.ride(8, X); part.stand(22);
    check(t.station() == 6, "back along X+: AUF180u");
    part.elevator(-1); part.stand(22);
    check(t.station() == 5, "back down the up-shaft: AUF180");
    part.ride(3, Ym + 180); part.stand(22);
    part.ride(3, Ym + 180); part.stand(22);
    check(t.station() == 3, "back along Y+: AUF170d");
    part.elevator(1); part.stand(22);
    check(t.station() == 2, "back up the down-shaft: AUF170");
    part.ride(8, X + 180); part.stand(22);
    part.ride(8, X + 180); part.stand(22);
    check(t.station() == 0, "back along X-: AUF120");
  }

  // --- Moves in Z at a station: lift (one move, either way) and RPS_seat
  // (set down on the reference pins and lifted back, either way first). The
  // station never changes. ---
  const auto rows = [](line::Tracker &t) { // Every log row so far, oldest first, as "file: csv".
    std::vector<std::string> v;
    const char *file, *csv;
    while (t.takeDistanceRow(file, csv)) v.push_back(std::string(file) + ": " + csv);
    return v;
  };
  const auto has = [](const std::string &row, const char *what) { return row.find(what) != std::string::npos; };
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- moves in Z at a station, structure shaking %s\n", shake > 0 ? "on" : "off");
    const auto run = [&](const char *motion, const char *range, auto &&body) {
      line::StationConfig cfg;
      char text[300];
      std::snprintf(text, sizeof text, "LW1,X+,0,true,%s,Z,%s\nLW2,X+,8,true,none,,\nLW3,X+,8,true,none,,\n", motion, range);
      check(line::parseStationConfig(text, cfg), motion);
      World world;
      auto tracker = std::make_unique<line::Tracker>();
      line::Tracker &t = *tracker;
      t.applyStationConfig(cfg);
      Part part{t, world};
      part.shake = shake;
      part.stand(10);
      t.home();
      part.stand(1);
      body(t, part);
    };
    for (float first : {-1.0f, 1.0f})
      run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
        part.elevator(0.5f * first); part.stand(10);
        check(rows(t).empty(), first < 0 ? "RPS_seat down first: nothing logged until the way back"
                                          : "RPS_seat up first: nothing logged until the way back");
        part.elevator(-0.5f * first); part.stand(12);
        const auto r = rows(t);
        std::printf("      cycle: %s", r.empty() ? "(none)\n" : r[0].c_str());
        check(r.size() == 1 && has(r[0], "LW1_tech: ") && has(r[0], ",RPS_seat,Z,0.") && has(r[0], "back_m=0.") &&
                !has(r[0], "over_range") && !has(r[0], "return_mismatch") && !has(r[0], "no_return") && has(r[0], ",0.500,"),
              "0.5 m and back, either way first: one row, the way back in back_m, declared 0.500");
        check(t.station() == 0 && t.transitCount() == 0, "the cycle changes no station and adds no transit");
        part.ride(8, 30); part.stand(22);
        check(t.station() == 1, "a ride along X after the cycle still works");
      });
    for (float gapS : {0.5f, 2.0f, 4.0f})
      run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
        part.elevator(-0.5f); part.stand(20);
        part.elevator(0.5f); part.stand(gapS); // The conveyor starts at once.
        part.ride(8, 30); part.stand(25);
        const auto r = rows(t);
        std::printf("      ride %.1f s after the way back: %zu rows, at %d\n", gapS, r.size(), t.station() + 1);
        check(t.station() == 1 && r.size() == 2 && has(r[0], "LW1_tech: ") && has(r[1], "LW1-LW2: ") &&
                std::fabs(column(r[1].substr(r[1].find(' ') + 1), 7) - 8) < 0.6f,
              "the conveyor starts right after the way back: the cycle and an 8 m ride, both logged");
      });
    run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.5f); part.stand(40); // Longer than the parking time.
      part.elevator(0.5f); part.stand(12);
      const auto r = rows(t);
      check(r.size() == 1 && has(r[0], "back_m=") && t.station() == 0, "a 40 s dwell on the pins: still one closed cycle");
    });
    run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.7f); part.stand(10);
      part.elevator(0.45f); part.stand(12);
      const auto r = rows(t);
      check(r.size() == 1 && has(r[0], "return_mismatch"), "way back 0.45 m of 0.7 m down: return_mismatch");
    });
    run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.9f); part.stand(10);
      part.elevator(0.9f); part.stand(12);
      const auto r = rows(t);
      check(r.size() == 1 && has(r[0], "over_range"), "0.9 m of 0.5 m range: over_range");
    });
    run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.5f); part.stand(660);
      const auto r = rows(t);
      check(r.size() == 1 && has(r[0], "no_return") && t.station() == 0, "no way back within 10 min: no_return");
    });
    run("RPS_seat", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.5f); part.stand(20);
      part.ride(8, 30); part.stand(22); // Leaves before the way back.
      const auto r = rows(t);
      std::printf("      left before the way back: %zu rows\n", r.size());
      check(r.size() == 2 && has(r[0], "LW1_tech: ") && has(r[0], "no_return") && has(r[1], "LW1-LW2: ") &&
              has(r[1], "transport"),
            "left before the way back: the open cycle (no_return), then the ride, both logged in order");
    });
    run("lift", "0.5", [&](line::Tracker &t, Part &part) {
      part.elevator(-0.5f); part.stand(20);
      auto r = rows(t);
      check(r.size() == 1 && has(r[0], "LW1_tech: ") && has(r[0], ",lift,Z,0.") && !has(r[0], "back_m") && t.station() == 0,
            "lift down: one row at once");
      part.elevator(0.5f); part.stand(20);
      r = rows(t);
      check(r.size() == 1 && has(r[0], ",lift,Z,0.") && t.station() == 0, "lift up: one more row (either way is fine)");
    });
    if (shake == 0) run("lift", "1.0", [&](line::Tracker &t, Part &part) { // Declared 1 m, really 0.3 m.
      part.elevator(-0.3f); part.stand(20);
      const auto r = rows(t);
      std::printf("      real 0.3 m of a declared 1 m: %s", r.empty() ? "(none)\n" : r[0].c_str());
      check(r.size() == 1 && (has(r[0], ",lift,Z,0.2") || has(r[0], ",lift,Z,0.3")) && has(r[0], "under_range") &&
              has(r[0], ",1.000,-0.7"),
            "a move well under the declared range: its measured depth, under_range, declared and delta");
    });
    run("RPS_seat", "0.4", [&](line::Tracker &t, Part &part) {
      part.stand(60);
      check(rows(t).empty() && t.station() == 0 && t.transitCount() == 0, "standing at an RPS_seat station: nothing logged");
      part.lift(); part.stand(5); // 0.4 m up and down with a slight tilt.
      check(t.station() == 0 && t.transitCount() == 0, "a lifting table's move changes no station");
    });
  }

  // --- Reference points: a turn or a move in Z that only some stations make
  // sets the station when the count is off, or finds it when it is unknown. ---
  for (float shake : {0.0f, 1.0f}) {
    std::printf("--- reference points, structure shaking %s\n", shake > 0 ? "on" : "off");
    const auto run = [&](const char *text, bool homed, auto &&body) {
      line::StationConfig cfg;
      check(line::parseStationConfig(text, cfg), "setup: parses");
      World world;
      auto tracker = std::make_unique<line::Tracker>();
      line::Tracker &t = *tracker;
      t.applyStationConfig(cfg);
      Part part{t, world};
      part.shake = shake;
      part.stand(60); // Learning the shaking before the first ride.
      if (homed) t.home();
      part.stand(1);
      body(t, part);
    };
    const char *rot = "R1,X+,0,true,none,,\nR2,X+,6.5,true,none,,\nR3,X+,6.5,true,none,,\n"
                      "R4,X+,6.5,true,rotation_z,Z,-90\nR5,X+,6.5,true,none,,\nR6,X+,6.5,true,none,,\n";
    run(rot, true, [&](line::Tracker &t, Part &part) {
      // Set at R1 while the body stood at R3: one ride later it is counted at R2.
      part.ride(6.5f, 30); part.stand(22);
      check(t.station() == 1, "setup: counted at R2 (really at R4)");
      rows(t);
      part.turntable(90); // Left by 90: only R4 does that.
      const auto r = rows(t);
      std::printf("      turn at R4 counted as R2: %s", r.empty() ? "(none)\n" : r.back().c_str());
      check(t.station() == 3, "a left turn by 90 counted at R2: the body is at R4 (the rotation_z station)");
      check(!r.empty() && has(r.back(), "R4_tech: ") && has(r.back(), "ref_fix=R2>R4") && has(r.back(), "rotation_z"),
            "logged at R4 with ref_fix=R2>R4");
      check(t.newRefFix() && std::string(t.refFixStation()) == "R4", "the fix is reported for the screen");
    });
    run(rot, false, [&](line::Tracker &t, Part &part) {
      check(!t.known(), "setup: station unknown after power-up");
      part.turntable(90);
      check(t.known() && t.station() == 3, "unknown, a left turn by 90: the only station that does it, R4");
    });
    run(rot, true, [&](line::Tracker &t, Part &part) {
      part.turntable(-90); // Right by 90 at R1: no station turns right.
      check(t.known() && t.station() == 0, "a turn no station makes changes nothing");
    });
    run("R1,X+,0,true,none,,\nR2,X+,6.5,true,rotation_z,Z,-90\nR3,X+,6.5,true,none,,\nR4,X+,6.5,true,rotation_z,Z,-90\n",
        true, [&](line::Tracker &t, Part &part) {
      part.ride(6.5f, 30); part.stand(22);
      part.ride(6.5f, 30); part.stand(22);
      check(t.station() == 2, "setup: counted at R3");
      part.turntable(90);
      check(t.station() == 2, "two turning stations as near (R2, R4): no change");
    });
    run("Q1,X+,0,true,none,,\nQ2,X+,6.5,true,none,,\nQ2r,RZ-,90,false,none,,\nQ3,X+,6.5,true,none,,\n", true,
        [&](line::Tracker &t, Part &part) {
      rows(t);
      part.turntable(90); // Counted at Q1, really at Q2 with its turntable.
      const auto r = rows(t);
      check(t.station() == 2, "a turntable's turn counted one station early: Q2, then through it to Q2r");
      check(!r.empty() && has(r.back(), "Q2-Q2r: ") && has(r.back(), "ref_fix=Q1>Q2"), "logged Q2-Q2r with ref_fix=Q1>Q2");
    });
    run("S1,X+,0,true,none,,\nS2,X+,8,true,none,,\nS3,X+,8,true,none,,\nS3d,Z-,1,false,none,,\nS4,Y-,3,true,none,,\n", true,
        [&](line::Tracker &t, Part &part) {
      rows(t);
      part.elevator(-1); part.stand(22); // Counted at S1, really at S3 above the shaft.
      const auto r = rows(t);
      std::printf("      shaft counted at S1: %s", r.empty() ? "(none)\n" : r.back().c_str());
      check(t.station() == 3, "1 m down counted at S1: the shaft's top S3, then down to S3d");
      check(!r.empty() && has(r.back(), "S3-S3d: ") && has(r.back(), "ref_fix=S1>S3"), "logged S3-S3d with ref_fix=S1>S3");
    });
    run("P1,X+,0,true,none,,\nP2,X+,8,true,none,,\nP3,X+,8,true,RPS_seat,Z,0.5\nP4,X+,8,true,none,,\n", true,
        [&](line::Tracker &t, Part &part) {
      rows(t);
      part.elevator(-0.5f); part.stand(10); // Counted at P1, really at P3.
      part.elevator(0.5f); part.stand(12);
      const auto r = rows(t);
      check(t.station() == 2, "set down on the pins counted at P1: the RPS_seat station P3");
      check(r.size() == 1 && has(r[0], "P3_tech: ") && has(r[0], "ref_fix=P1>P3") && has(r[0], "back_m="),
            "one cycle logged at P3 with ref_fix=P1>P3");
    });
  }

  // --- Without a valid config.csv nothing is tracked. ---
  {
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    Part part{t, world};
    part.stand(20);
    part.ride(8, 30);
    part.stand(20);
    const char *file, *csv;
    check(t.phase() == line::Tracker::Phase::Settling && !t.known() && t.transitCount() == 0 && !t.takeDistanceRow(file, csv),
          "no config.csv: no position, no rides, no log rows");
  }

  // --- Declared vs measured: rows carry what config.csv says and the difference. ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,wifi_scan,station_motion,motion_axis,motion_range\n"
      "D1,X+,0,true,none,,\n"
      "D2,X+,6,true,none,,\n"
      "D3,X+,6,true,none,,\n";
    check(line::parseStationConfig(text, cfg), "setup: declared-vs-measured line parses");
    World world;
    auto tracker = std::make_unique<line::Tracker>();
    line::Tracker &t = *tracker;
    t.applyStationConfig(cfg);
    Part part{t, world};
    part.stand(10);
    t.home();
    part.stand(1);
    part.ride(7, 30); part.stand(22); // Declared 6 m, really 7.
    const std::string r = t.newDistanceRow() ? t.distanceRowCsv() : "";
    std::printf("      transport row: %s", r.c_str());
    int commas = 0;
    for (char c : r) commas += c == ',';
    check(commas == 11 + 2 + 7, "transport row: 11 columns, declared, delta, 8 ride columns");
    check(r.find(",6.000,0.9") != std::string::npos,
          "transport row: declared 6.000, delta about +1 (7 m ridden)");
  }

  {
    line::Tracker t;
    line::StationConfig cfg;
    check(line::parseStationConfig("A,X+,0,true,none,,\nB,X+,6,true,none,,\n",cfg),"clock test config");
    t.applyStationConfig(cfg);
    for (uint32_t us=1000;us<=1001000;us+=5000) t.add({0,0,9.81f},{0,0,0},us);
    check(t.clockMs()>=999 && t.clockMs()<=1001,"tracker clock follows sample time");
    auto before=t.clockMs(); t.home(); t.breakSignal();
    check(t.clockMs()==before,"home and signal break preserve tracker clock");
  }
  return failures ? 1 : 0;
}
