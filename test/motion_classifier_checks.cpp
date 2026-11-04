// test/motion_classifier_checks.cpp
// g++ -std=c++17 -Iinclude test/motion_classifier_checks.cpp -o mcc && ./mcc
#include <cmath>
#include <cstdio>
#include "MotionClassifier.h"
#include "StationConfig.h"

namespace {
int failures = 0;
void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}

int main() {
  using namespace line;

  {
    StationRow none;
    none.motion = Motion::None;
    const MotionResult r = classifyMotion(none, 0.3f, 0);
    check(!r.valid, "station with motion=none produces no classification");
  }
  {
    StationRow lift;
    lift.motion = Motion::Lift;
    lift.motionRange = 0.4f;
    const MotionResult r = classifyMotion(lift, 0.38f, 0);
    check(r.valid, "lift within range classified");
    check(std::fabs(r.measuredDistanceM - 0.38f) < 1e-6f, "measured distance recorded");
    check(!r.overRange, "within 1.5x range is not flagged");
  }
  {
    StationRow lift;
    lift.motion = Motion::Lift;
    lift.motionRange = 0.4f;
    const MotionResult r = classifyMotion(lift, 0.9f, 0); // 2.25x configured range
    check(r.valid, "lift still classified when it exceeds the configured range");
    check(r.overRange, "exceeding 1.5x range is flagged, not silently reclassified");
  }
  {
    StationRow rot;
    rot.motion = Motion::RotationZ;
    rot.motionRange = 90;
    const MotionResult r = classifyMotion(rot, 0, 91.0f);
    check(r.valid, "rotation classified");
    check(std::fabs(r.measuredAngleDeg - 91.0f) < 1e-6f, "measured angle recorded, not distance");
    check(std::fabs(r.measuredDistanceM) < 1e-6f, "distance field unused for rotation");
    check(!r.overRange, "91 deg vs 90 deg configured is within 1.5x");
  }

  std::printf(failures ? "%d FAILURE(S)\n" : "all ok\n", failures);
  return failures ? 1 : 0;
}
