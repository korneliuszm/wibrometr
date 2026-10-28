// Host checks of DisplacementMeter on synthetic 200 Hz accelerometer data:
//   g++ -std=c++17 -O2 -Iinclude test/displacement_checks.cpp -o /tmp/dc && /tmp/dc
#include <cmath>
#include <cstdio>
#include <memory>
#include "Displacement.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

struct Feed {
  DisplacementMeter &m;
  Accel gravity;                         // Reaction to gravity in the world axes.
  uint32_t us = 0;
  uint32_t seed = 1;
  float noise = 0.04f; // m/s2 RMS-ish, close to the MPU6050 at 94 Hz bandwidth.
  bool gyro = true;    // False: feed zero rates, as without a gyro.
  Accel bias{0.05f, -0.03f, 0.02f};      // Gyro zero-rate offset, rad/s (~3 deg/s).
  float theta = 0, thetaRate = 0;        // Turn of the sensor about Y, rad.
  float rnd() { // Deterministic, uniform in [-1, 1].
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) * (2.0f / 16777216.0f) - 1.0f;
  }
  // World-axes acceleration (without gravity) seen by the turned sensor.
  void sample(float ax, float ay, float az) {
    us += 5000;
    const float wx = gravity.x + ax, wy = gravity.y + ay, wz = gravity.z + az;
    const float c = std::cos(theta), s = std::sin(theta); // Body = Ry(-theta) * world.
    const Accel body{c * wx - s * wz + noise * 1.7f * rnd(), wy + noise * 1.7f * rnd(),
                     s * wx + c * wz + noise * 1.7f * rnd()};
    const float gn = 0.002f; // rad/s, gyro noise.
    const Accel w = gyro ? Accel{bias.x + gn * rnd(), bias.y + thetaRate + gn * rnd(), bias.z + gn * rnd()}
                         : Accel{};
    m.add(body, w, us);
  }
  // Minimum-jerk move of dx metres along X while turning by `turn` rad about Y.
  void moveTurning(float dx, float turn, float s) {
    const int n = int(s * 200);
    const float start = theta;
    for (int i = 0; i < n; ++i) {
      const float t = float(i) / n;
      theta = start + turn * (10 * t * t * t - 15 * t * t * t * t + 6 * t * t * t * t * t);
      thetaRate = turn / s * (30 * t * t - 60 * t * t * t + 30 * t * t * t * t);
      sample(dx * (60 * t - 180 * t * t + 120 * t * t * t) / (s * s), 0, 0);
    }
    theta = start + turn;
    thetaRate = 0;
  }
  void rest(float s) { for (int i = 0; i < int(s * 200); ++i) sample(0, 0, 0); }
  // Minimum-jerk move of (dx, dy, dz) metres in `s` seconds.
  void move(float dx, float dy, float dz, float s) {
    const int n = int(s * 200);
    for (int i = 0; i < n; ++i) {
      const float t = float(i) / n;
      const float k = (60 * t - 180 * t * t + 120 * t * t * t) / (s * s);
      sample(dx * k, dy * k, dz * k);
    }
  }
};
bool near(float got, float want, float tol) { return std::fabs(got - want) <= tol; }
} // namespace

int main() {
  auto meter = std::make_unique<DisplacementMeter>();
  DisplacementMeter &m = *meter;
  Feed f{m, {0.3f, -0.2f, 9.9f}}; // Uncalibrated zero offset on every axis.

  f.rest(1.0f);
  check(m.state() == DisplacementMeter::State::Rest, "rest is detected");
  f.move(0.30f, 0, 0, 1.0f);
  f.rest(1.0f);
  std::printf("      X move: %.4f %.4f %.4f m, %.2f s, %.2f m/s\n", m.last().d[0], m.last().d[1],
              m.last().d[2], m.last().durationS, m.last().peakSpeed);
  check(m.moves() == 1, "one move recorded");
  check(near(m.last().d[0], 0.30f, 0.01f), "30 cm along X within 1 cm");
  check(near(m.last().d[1], 0, 0.01f) && near(m.last().d[2], 0, 0.01f), "Y and Z stay near zero");
  check(near(m.last().peakSpeed, 0.30f * 1.875f, 0.05f), "peak speed of a min-jerk move");
  check(!m.last().tilted, "no tilt flagged");
  check(m.last().points > 50 && near(m.last().track[m.last().points - 1][0], m.last().d[0], 1e-4f),
        "track ends at the result");

  f.move(-0.10f, 0.20f, 0.05f, 1.5f);
  f.rest(1.0f);
  std::printf("      oblique: %.4f %.4f %.4f m\n", m.last().d[0], m.last().d[1], m.last().d[2]);
  check(near(m.last().d[0], -0.10f, 0.01f) && near(m.last().d[1], 0.20f, 0.01f) &&
        near(m.last().d[2], 0.05f, 0.01f), "oblique move in all axes");
  check(near(m.total(0), 0.20f, 0.02f) && near(m.total(1), 0.20f, 0.02f), "totals add up");

  f.move(0, 0, 0.5f, 2.0f);
  f.rest(1.0f);
  std::printf("      slow Z: %.4f m\n", m.last().d[2]);
  check(near(m.last().d[2], 0.5f, 0.03f), "50 cm in 2 s along Z within 3 cm");

  f.move(0.15f, 0, 0, 0.6f); // There and back: net zero.
  f.move(-0.15f, 0, 0, 0.6f);
  f.rest(1.0f);
  std::printf("      there and back: %.4f m\n", m.last().d[0]);
  check(near(m.last().d[0], 0, 0.01f), "there and back is one move with net zero");

  // Turning by 30 deg during a 30 cm move: the gyro keeps gravity in place.
  f.moveTurning(0.30f, 0.5236f, 1.0f);
  f.rest(0.8f);
  std::printf("      turning 30 deg: %.4f %.4f %.4f m, turn %.1f deg\n", m.last().d[0], m.last().d[1],
              m.last().d[2], m.last().rotationDeg);
  check(near(m.last().d[0], 0.30f, 0.015f) && near(m.last().d[2], 0, 0.015f), "turning move within 1.5 cm");
  check(near(m.last().rotationDeg, 30, 1) && !m.last().tilted, "turn measured, not flagged");
  // Back and level again. The result is in the axes at the start of this move,
  // which are turned by 30 deg: -20 cm world X = (-17.3, 0, -10) cm there.
  f.moveTurning(-0.20f, -0.5236f, 1.0f);
  f.rest(0.8f);
  std::printf("      back: %.4f %.4f %.4f m\n", m.last().d[0], m.last().d[1], m.last().d[2]);
  check(near(m.last().d[0], -0.1732f, 0.015f) && near(m.last().d[2], -0.10f, 0.015f) && !m.last().tilted,
        "move back while turning back, in the turned axes");

  { // The same turning move without the gyro is flagged.
    auto plainMeter = std::make_unique<DisplacementMeter>();
    Feed p{*plainMeter, {0.3f, -0.2f, 9.9f}};
    p.gyro = false;
    p.rest(1.0f);
    p.moveTurning(0.30f, 0.5236f, 1.0f);
    p.rest(2.0f);
    std::printf("      no gyro: %.4f %.4f m\n", plainMeter->last().d[0], plainMeter->last().d[2]);
    check(plainMeter->moves() == 1 && plainMeter->last().tilted, "without the gyro a turn is flagged");
  }

  // Tilt: gravity ends up in another direction.
  f.move(0.1f, 0, 0, 1.0f);
  f.gravity = {2.0f, -0.2f, 9.7f};
  f.rest(2.0f); // Off the old gravity a rest needs 1 s of quiet.
  check(m.last().tilted, "tilt during the move is flagged");

  // A long continuous shake never comes to rest.
  const uint32_t before = m.moves();
  for (int i = 0; i < 20 * 200; ++i) f.sample(3.0f * std::sin(i * 0.2f), 0, 0);
  check(m.state() == DisplacementMeter::State::TooLong, "move over 15 s is abandoned");
  f.rest(1.0f);
  check(m.moves() == before && m.state() == DisplacementMeter::State::Rest, "and no result is made");

  m.resetTotal();
  check(m.total(0) == 0 && m.moves() == 0, "reset clears the totals");
  return failures ? 1 : 0;
}
