// Host checks of line::Tracker on a simulated roller line:
//   g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc
// (add -DLINE_SAMPLE_HZ=1000 to check the firmware's 1 kHz rate)
//
// The line: 8 tables, gaps along X and Y on the floor, a lifting table
// (table 2, with a slight tilt) and two turntables (table 3 turns +90 deg,
// table 5 -90 deg). Every scenario runs with and without the structure
// shaking.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include "LineTracker.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
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
    const V g = toSensor(rate);
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
  void turntable(float angDeg) {
    const float T = 10;
    for (float tt = 0; tt < T; tt += step) {
      const float s = std::sin(3.14159f * tt / T);
      sample(0, 0, 0, angDeg * deg / T * 2 * s * s);
    }
    stand(3);
  }
  // Carried by hand to (tx, ty) in `secs`: walking, rocking +-12 deg,
  // turned 60 deg and back, bobbing up and down; set down gently.
  void carry(float tx, float ty, float secs) {
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
      sample(ax, ay, 1.5f * std::sin(2 * 3.14159f * 1.8f * tt) * std::sin(3.14159f * u), yaw, sway);
    }
    x = tx; y = ty;
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

void suite(float shake) {
  std::printf("--- structure shaking %s\n", shake > 0 ? "on" : "off");
  World world;
  auto tracker = std::make_unique<line::Tracker>();
  line::Tracker &t = *tracker;
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
    if (s == 1) { // Lifting table at table 2.
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
  check(liftQuiet, "lifting table: no ride, same table");
  check(worstLevel < (shake > 0 ? 0.15f : 0.08f), "lift and turns do not raise the thresholds");
  check(std::fabs(t.gap(3) - 8.0f) < 0.6f, "spacing 4-5 learned (8 m)");
  check(t.learned.gapDirKnown[2] && std::fabs(line::wrap(t.learned.gapDir[2] - 60 * deg)) < 10 * deg,
        "gap 3-4 learned along the floor's Y (90 deg from table 1's heading of 30)");
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
  part.carry(tableX[2], tableY[2], 20);
  part.stand(4);
  check(t.carrying(), "being carried is recognised right away");
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

  // Power cycle: what was learned is kept; Wi-Fi finds the table and its heading.
  auto fresh = std::make_unique<line::Tracker>();
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
    t.setStations(2);
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

  // --- CSV-authoritative: config.csv overrides auto-learning ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed\n"
      "ST01,X+,10,false,true,none,,,\n"
      "ST02,X+,10,false,true,none,,,\n"; // CSV says 10 m; the simulated ride below is only 6 m.
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
    check(std::fabs(t.gap(0) - 10.0f) < 1e-3f,
          "gap(0) is unchanged by the actual 6 m ride: auto-learning is disabled");
  }

  // --- RotationZ technological logging fires from the turn event itself
  // (Tracker::endTurn()), not from finish(): a turn recognised while parked
  // never reaches finish() (only called from Phase::Moving). ---
  {
    line::StationConfig cfg;
    const char *text =
      "station_id,direction,distance_m,passthrough,wifi_scan,station_motion,motion_axis,motion_range,motion_speed\n"
      "ST01,X+,20,false,true,rotation_z,Z,90,\n"
      "ST02,X+,20,false,true,none,,,\n";
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
    check(row.find(",,90.0") != std::string::npos,
          "rotation_z: measured_angle_deg is ~90 deg, not 0");
  }

  return failures ? 1 : 0;
}
