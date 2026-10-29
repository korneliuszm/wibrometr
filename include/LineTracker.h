#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "DistanceLog.h"
#include "MotionClassifier.h"
#include "SignalHistory.h"
#include "StationConfig.h"

// Sensor output rate the tracker is built for (the firmware sets it).
#ifndef LINE_SAMPLE_HZ
#define LINE_SAMPLE_HZ 200
#endif

// Position of a car body riding a roller line between numbered tables
// (stations), as config.csv describes the line: gaps along X and Y, lift
// shafts, turntables (multiples of 90 degrees) and technological motions at
// stations. Nothing about the line is learned; without a valid config.csv
// nothing is tracked.
//
// Attitude: the gyro (minus its offset) turns the expected gravity in the
// sensor axes all the time, standing or riding; the accelerometer only pulls
// it back slowly. So tilting (lifting tables) or turning (turntables) does not
// look like acceleration. The heading (turn about the vertical) is integrated
// the same way; a turn while standing is reported, and the heading snapped to
// a multiple of 90 degrees when close to one (logs keep the measured angle).
//
// Standing or riding: the structure the tables hang from shakes the body all
// the time. A ride is told apart by frequency and direction: a horizontal
// "leaky velocity" (acceleration without gravity, integrated with a 1 s leak)
// is large on a ride (0.3+ m/s) and small for shaking (amplitude A at f Hz
// gives A / 2 pi f). Its fluctuation and that of the low-passed acceleration
// are measured while standing, and the thresholds follow them. A ride ends
// with braking (the leaky velocity against the way it started) and calm.
//
// Distance: the ride is integrated in the axes at its start (gyro-turned,
// gravity removed). At the stop velocity is zero, so the velocity left over is
// drift and is removed in closed form; so is the gravity mismatch left by the
// gyro. The horizontal part of the result is the distance, and its direction
// plus the heading gives the direction on the floor.
//
// Vertical: the ride detector watches it everywhere. Next to a lift shaft
// (a Z+/Z- gap) a move the way of the shaft goes to the next stop. At a
// station with a technological move in Z (lift: one move; RPS_seat: down onto
// the reference pins and back, either way first) the station stays and the
// move is logged in <station>_tech.csv (a leg ends after `legStopS` of calm).
//
// Tables: the gaps' directions (in the sensor's axes) tell a ride forward
// from back, and a ride moves the position by distance / spacing tables
// (rounded). Rides without braking (a tilt or a jolt) move nothing.
//
// Reference points: a turn or a move in Z that only some stations make (a
// turntable, rotation_z, a shaft, lift or RPS_seat) and that the counted
// station cannot explain puts the body at the nearest station that can (if
// one is clearly nearest; while the station is unknown, if it is the only one
// on the line). The log row carries "ref_fix=A>B".
//
// Wi-Fi: a stop longer than `parkS` confirms the table and asks for a Wi-Fi
// fingerprint (only where config.csv says wifi_scan). The first one at a table
// becomes its reference. Scans never overrule a counted table; they find it
// while it is unknown (after a power cycle, lost samples or being carried).
//
// Each ride also records its vibration: RMS and peak of the high-passed
// acceleration per axis, the largest ramp (smoothed) acceleration and an
// envelope of the acceleration without gravity over time.
namespace line {
constexpr int maxStations = 64;
constexpr int maxAps = 24;           // Strongest access points kept per fingerprint.
constexpr int envBins = 160;         // Envelope points kept per transit.
constexpr int maxTransits = 12;      // Recent transits kept.
constexpr float missingDbm = -95;    // RSSI assumed for an access point not heard.
constexpr float pi = 3.14159265f;

struct Ap { uint8_t bssid[6]; float rssi; };
struct Fingerprint {
  int count = 0; // 0: none.
  Ap ap[maxAps];
};

// Mean absolute RSSI difference (dB) over the access points heard in either
// fingerprint; one not heard counts as `missingDbm`. Lower is more alike.
inline float distance(const Fingerprint &a, const Fingerprint &b) {
  float sum = 0;
  int n = 0;
  const auto find = [](const Fingerprint &f, const uint8_t *bssid) {
    for (int i = 0; i < f.count; ++i)
      if (std::memcmp(f.ap[i].bssid, bssid, 6) == 0) return f.ap[i].rssi;
    return missingDbm;
  };
  for (int i = 0; i < a.count; ++i, ++n) sum += std::fabs(a.ap[i].rssi - find(b, a.ap[i].bssid));
  for (int i = 0; i < b.count; ++i)
    if (find(a, b.ap[i].bssid) == missingDbm) { sum += std::fabs(b.ap[i].rssi - missingDbm); ++n; }
  return n ? sum / n : 99;
}
// Moves `into` toward `scan` by `alpha`, keeping the strongest `maxAps`.
inline void blend(Fingerprint &into, const Fingerprint &scan, float alpha) {
  Fingerprint out;
  const auto rssiIn = [](const Fingerprint &f, const uint8_t *bssid) {
    for (int i = 0; i < f.count; ++i)
      if (std::memcmp(f.ap[i].bssid, bssid, 6) == 0) return f.ap[i].rssi;
    return missingDbm;
  };
  Ap all[2 * maxAps];
  int n = 0;
  for (int i = 0; i < into.count; ++i) all[n++] = into.ap[i];
  for (int i = 0; i < scan.count; ++i)
    if (rssiIn(into, scan.ap[i].bssid) == missingDbm) all[n++] = scan.ap[i];
  for (int i = 0; i < n; ++i) {
    const float old = rssiIn(into, all[i].bssid), now = rssiIn(scan, all[i].bssid);
    all[i].rssi = old + alpha * (now - old);
  }
  for (int i = 1; i < n; ++i) // Strongest first.
    for (int j = i; j > 0 && all[j].rssi > all[j - 1].rssi; --j) { const Ap t = all[j]; all[j] = all[j - 1]; all[j - 1] = t; }
  out.count = n < maxAps ? n : maxAps;
  for (int i = 0; i < out.count; ++i) out.ap[i] = all[i];
  into = out;
}
// Angle wrapped to (-pi, pi].
inline float wrap(float a) {
  while (a > pi) a -= 2 * pi;
  while (a <= -pi) a += 2 * pi;
  return a;
}

struct Transit {
  uint32_t id = 0;
  int from = 0, to = 0;          // Tables, 0-based; -1: unknown.
  float distance = 0, durationS = 0;
  float floorDeg = 0;            // Direction on the floor (0 = forward at table 1, CCW).
  float ramp = 0;                // Largest smoothed acceleration (start/brake), m/s2.
  float rms[3] = {}, peak[3] = {}; // Vibration (high-passed), m/s2.
  bool wifiCorrected = false;    // The table was changed by the Wi-Fi match.
  bool carried = false;          // Not a roller ride: moved by hand (or crane); table from Wi-Fi.
  int8_t lift = 0;               // Up (+1) or down (-1) a lift shaft; 0: along the floor.
  int bins = 0;
  float binS = 0;                // Time per envelope point.
  int16_t lo[envBins][3] = {}, hi[envBins][3] = {}; // Acceleration without gravity, 0.01 m/s2.
};

// What is learned and kept across power cycles: Wi-Fi fingerprints and the
// body's heading at each table. The line itself comes from config.csv only.
struct Learned {
  static constexpr uint32_t currentVersion = 4; // 4: the line from config.csv only.
  uint32_t version = currentVersion;
  float heading[maxStations] = {};   // Heading of the body when it last stood at table i, rad.
  bool headingKnown[maxStations] = {};
  Fingerprint fp[maxStations];
};

} // namespace line
namespace line {

static_assert(StationConfig::maxRows == maxStations,
              "StationConfig::maxRows mirrors line::maxStations; keep them equal.");

class Tracker {
public:
  static constexpr int restSamples = LINE_SAMPLE_HZ * 3 / 2; // 1.5 s: pre-roll and first estimates.
  static constexpr float settleS = 8.0f;       // Learning the shaking before the first stand.
  static constexpr float gravityTauS = 3.0f;   // Accelerometer pull on the gyro-turned gravity.
  static constexpr float biasTauS = 4.0f;      // Gyro offset: two-stage average when not turning.
  static constexpr float gyroGate = 0.004f;    // rad/s (0.23 deg/s, 1 s mean): above this the body turns/tilts.
  static constexpr float gateGiveUpS = 30.0f;  // Standing "turned" this long: it is the offset that moved.
  static constexpr float levelTauS = 10.0f;    // Memory of the shaking level.
  static constexpr float lpTauS = 0.15f;       // Two-stage low-pass of the acceleration (~1 Hz).
  static constexpr float leakTauS = 1.5f;      // Leak of the velocity used to spot a ride (longer: more of a ride clears the high threshold).
  static constexpr float shaftLeakTauS = 3.0f; // The same, vertical, next to a lift shaft.
  static constexpr float startK = 4.0f, minStartV = 0.25f, maxStartV = 0.30f;  // m/s; the floor keeps shaking out.
  static constexpr float minShaftV = 0.05f, maxShaftV = 0.25f;                   // m/s, vertical, next to a lift shaft.
  static constexpr float brakeK = 0.5f;        // Braking: this share of the start threshold.
  static constexpr float stillK = 3.5f, minStillA = 0.05f, maxStillA = 0.4f;   // m/s2
  static constexpr float startConfirmS = 0.15f;
  // Calm this long ends a ride without braking. A steady roll is as calm as
  // standing, so this must outlast the longest roll: a ride through
  // passthrough tables (24 m at 0.6 m/s rolls ~38 s between its ramps).
  static constexpr float forcedStopS = 45.0f;
  static constexpr float stopS = 2.0f;         // Calm this long: shown as arrived.
  static constexpr float legStopS = 6.0f;      // Calm this long ends a technological lift leg (gravity is re-measured over it).
  static constexpr float minLegM = 0.1f;      // A technological lift leg is at least this far; the configured range does not gate it (it may be a guess).
  static constexpr float underRangeK = 0.5f;  // A leg under this share of the configured range: "under_range".
  static constexpr float cycleReturnS = 600.0f; // A down-and-back cycle must be back within this.
  static constexpr float returnTol = 0.2f;     // Way back differing from the way out by this share
  static constexpr float returnTolM = 0.15f;   // and this much (the way back spreads +-0.12 m under strong shaking): "return_mismatch".
  static constexpr float parkS = 15.0f;        // Calm this long: table confirmed, Wi-Fi scan.
  static constexpr float turnStart = 0.035f;   // rad/s (2 deg/s) about the vertical: turning.
  static constexpr float turnEnd = 0.009f;     // rad/s (0.5 deg/s) ...
  static constexpr float turnEndS = 1.0f;      // ... this long: the turn is over.
  static constexpr float minTurn = 0.35f;      // rad (20 deg): smaller is not a turn.
  static constexpr float snapTol = 0.35f;      // rad: within this of k * 90 deg, snap to it.
  static constexpr float minTransitM = 1.5f;   // Shorter: a nudge, same table.
  static constexpr float nudgeShare = 0.35f;   // Under this share of a spacing: same table.
  static constexpr float maxTransitS = 60.0f;  // Longer: not a ride, table unknown.
  static constexpr float abandonS = 120.0f;    // "Riding" this long: the attitude is lost; start over.
  // Carried elsewhere instead of riding the rollers: a roller ride stays
  // level, does not turn and ends with braking.
  static constexpr float carryTilt = 0.14f;    // rad (8 deg) of tilt during the move.
  static constexpr float carryYaw = 0.52f;     // rad (30 deg) of turn before the stop.
  static constexpr float carrySettleS = 3.0f;  // Set down and still this long: scan Wi-Fi.
  static constexpr float carryStillRate = 0.05f; // rad/s (3 deg/s, 0.5 s mean): no longer rocked by a hand.
  static constexpr float carryStillA = 0.15f;    // m/s2: raw acceleration this steady (0.2 s vs 1 s mean).
  static constexpr float turntableTilt = 0.05f;  // rad/s (3 deg/s): turning with less tilt than this is a turntable.
  // Finding the table by Wi-Fi alone: the best table must match well and
  // clearly better than the next one, else scan again later.
  static constexpr float relocateMaxDb = 12.0f;
  static constexpr float relocateMarginDb = 0.8f;   // At least this and
  static constexpr float relocateMarginShare = 0.25f; // this share of the best score.
  static constexpr float relocateRetryS = 45.0f;
  // Why the table is not known.
  enum class Lost : uint8_t { None, Boot, SamplesLost, Carried };
  static constexpr float maxGapS = 0.05f;
  static constexpr float vibTauS = 0.25f;      // Ramp / vibration split (~0.6 Hz).
  static constexpr float workBinS = 0.05f;
  static constexpr int workBins = 600;
  enum class Phase : uint8_t { Settling, Parked, Moving };

  Learned learned;

  // Loads a parsed config.csv: the line (stations, gaps, directions, shafts,
  // turntables, technological motions). Nothing about the line is learned;
  // without a config the tracker does nothing at all (add() returns). Wi-Fi
  // never overrules a counted station, it only finds one while it is unknown.
  // Call once, at boot, before `home()`.
  void applyStationConfig(const StationConfig &cfg) {
    stations_ = cfg.count < 2 ? 2 : cfg.count > maxStations ? maxStations : cfg.count;
    if (station_ >= stations_) station_ = stations_ - 1;
    // Directions are in the sensor's (the body's) axes: the body turns with
    // the sensor on a turntable, so "X+" stays the sensor's X+ after it while
    // the line goes on turned on the floor. Rides are compared in those axes
    // (see direction()); shownDir() turns them back onto the floor to draw.
    float turned = 0;
    for (int i = 0; i < cfg.count; ++i) {
      stationRow_[i] = cfg.row[i];
      if (i + 1 < cfg.count) {
        const Direction d = cfg.row[i + 1].direction;
        gap_[i] = cfg.row[i + 1].distanceM;
        shaft_[i] = d == Direction::ZPlus ? 1 : d == Direction::ZMinus ? -1 : 0;
        // RZ is about the Z axis as shown (pointing down): RZ+ turns right (CW from above).
        turn_[i] = d == Direction::RZPlus ? -1 : d == Direction::RZMinus ? 1 : 0;
        // A turn before this gap: at station i (rotation_z, signed range about
        // Z as shown, down: + right) or this gap itself (RZ). CCW is + here.
        if (cfg.row[i].motion == Motion::RotationZ) turned -= cfg.row[i].motionRange * pi / 180;
        if (turn_[i]) turned += turn_[i] * cfg.row[i + 1].distanceM * pi / 180;
        gapDir_[i] = shaft_[i] || turn_[i] ? 0 : directionAngle(d);
        gapFloor_[i] = !shaft_[i] && !turn_[i]; // No floor direction: not ridden along the floor.
        shownDir_[i] = wrap(gapDir_[i] + turned);
      }
    }
    configAuthoritative_ = true;
  }
  bool configAuthoritative() const { return configAuthoritative_; }
  // Configured row for station `s`, or nullptr if no config.csv was loaded
  // or `s` is out of range. Used by the motion classifier and distance log
  // in later tasks.
  const StationRow *stationRow(int s) const {
    return configAuthoritative_ && s >= 0 && s < stations_ ? &stationRow_[s] : nullptr;
  }
  // The body is at table 1 now; its heading there is the floor's 0.
  void home() {
    pendDir_ = 0;
    station_ = 0;
    known_ = true;
    lost_ = Lost::None;
    heading_ = 0;
    headingKnown_ = true;
    if (phase_ == Phase::Moving) phase_ = Phase::Settling;
    if (phase_ == Phase::Parked) wantScan();
  }
  void forgetWifi() { for (auto &f : learned.fp) f.count = 0; lastScan_.count = 0; }
  // In place: a fresh Learned would be ~5 KB on the stack.
  void forgetAll() {
    learned.version = Learned::currentVersion;
    for (int i = 0; i < maxStations; ++i) { learned.heading[i] = 0; learned.headingKnown[i] = false; }
    forgetWifi();
  }
  int stations() const { return stations_; }

  void add(Accel a, Accel w, uint32_t us) {
    if (!configAuthoritative_) return; // No valid config.csv: the line is not tracked at all.
    float dt = 0;
    if (primed_) {
      dt = (us - lastUs_) * 1e-6f;
      if (dt <= 0 || dt > maxGapS) breakSignal();
    }
    lastUs_ = us;
    primed_ = true;
    clock_ += dt;
    ring_[head_] = {a, w, dt};
    head_ = (head_ + 1) % restSamples;
    if (ringCount_ < restSamples) ++ringCount_;
    if (phase_ == Phase::Moving) step(a, w, dt);
    if (ringCount_ < restSamples) return;
    if (!refSet_) {
      gB_ = mean(&Sample::a);
      gMid_ = gB_;
      if (!biasSet_) { bias_ = biasMid_ = mean(&Sample::w); biasSet_ = true; }
      for (Rest &h : hist_) h = {gB_, bias_};
      refSet_ = true;
      settleStart_ = clock_;
    }
    attitude(a, w, dt);
    detect(a, dt);
    switch (phase_) {
      case Phase::Settling:
        follow(dt, 1.0f); // Learn the shaking quickly.
        if (clock_ - settleStart_ >= settleS) enterPark();
        break;
      case Phase::Parked: {
        if (pendDir_ && clock_ - pendAt_ > cycleReturnS) flushLiftCycle("no_return");
        // While the body turns level (turntable) nothing starts a ride: a
        // sensor off the table's axis swings along an arc, which is no ride.
        // The detector is held at zero meanwhile and the arc is kept out of
        // the shaking level, so a ride out right after the turn starts clean.
        // A hand tilts the body as well: that is a carry and may start.
        const bool turningNow = std::fabs(yawFast_) > 0.5f * turnStart && tiltLp_ < turntableTilt;
        sinceTurn_ = turningNow ? 0 : std::min(sinceTurn_ + 1, restSamples);
        if (turningNow) {
          clearDetector();
          overS_ = 0;
          hist_[(head_ + restSamples - 1) % restSamples] = {gB_, bias_}; // As follow() would.
        } else follow(dt, levelTauS);
        if (!turningNow && (hv_ > startThreshold() || std::fabs(vv_) > shaftThreshold())) {
          if ((overS_ += dt) >= startConfirmS) startMove();
        } else overS_ = 0;
        if (phase_ == Phase::Parked && !parkDone_ && clock_ - phaseStart_ >= parkS) { parkDone_ = true; wantScan(); }
        // Table still unknown after a scan: try again now and then.
        if (phase_ == Phase::Parked && !known_ && relocateAt_ >= 0 && clock_ >= relocateAt_) {
          relocateAt_ = -1;
          wantScan();
        }
        break;
      }
      case Phase::Moving: {
        // Safety net: no ride or carry lasts this long. Whatever happened,
        // the attitude cannot be trusted: learn gravity again, let Wi-Fi
        // find the table.
        if (t_ > abandonS) { abandon(); break; }
        // Braking: the leaky velocity points against the way the ride
        // started. Speeding up again after it cancels it.
        // Up or down a shaft: the same on the vertical leaky velocity.
        const float along = rideUp_ ? vv_ * float(rideUp_) : dot(vlFlat_, rideDir_);
        // Braking is looked for only within a ride: half the start threshold
        // (which stays high to keep the shaking out) is enough.
        const float limit = rideUp_ ? shaftThreshold() : brakeK * startThreshold();
        if (along < -limit) braked_ = true;
        else if (along > limit) braked_ = false;
        // Calm: no ride-like velocity and little low-frequency acceleration.
        // A steady roll is calm too, so a stop also needs the braking before
        // it (or a calm longer than any steady roll).
        // Carried: the attitude the gyro kept may be off (a knock past its
        // range), so the stop is told from the raw sensor alone: no rocking
        // and a steady accelerometer once the hand has let go.
        // A move in Z (started by the vertical, not next to a shaft: a
        // technological move, or a reference point while the count is off or
        // unknown) is told by the vertical alone: the tilt of a lifting table
        // drifts the horizontal signals for good.
        const bool vertLeg = !carrying() && rideUp_ && !(known_ && atShaft(station_));
        const bool calm = vertLeg ? std::fabs(vv_) < shaftThreshold() && std::fabs(va_) < stillVThreshold() : carrying()
          ? rideRate_ < std::max(carryStillRate, 2.5f * std::sqrt(levelW2_)) &&
              norm(sub(aFast_, aSlow_)) < std::max(carryStillA, 3 * std::sqrt(levelA2_))
          : ha_ < stillThreshold() && hv_ < startThreshold() &&
              (!(rideUp_ || (known_ && atShaft(station_))) || std::fabs(vv_) < shaftThreshold()); // Vertical: up a shaft only.
        // The rollers start during a move in Z or before its calm is over (a
        // table lifts the body and the conveyor goes at once): from here on it
        // is a ride. The integration goes on from the start of the move in Z,
        // so the ride's distance is the horizontal part and that move the
        // vertical one (see finish()). Counted from the start of the move, so
        // the slow drift a tilted lifting table leaves is not a start.
        if (vertLeg && norm(sub(vlFlat_, vlLeg_)) > startThreshold()) {
          if ((overS_ += dt) >= startConfirmS) {
            const Accel go = sub(vlFlat_, vlLeg_);
            rideDir_ = scale(go, 1 / norm(go));
            rideUp_ = 0;
            legThenRide_ = true;
            braked_ = false;
            quietStart_ = -1;
            arrived_ = false;
            overS_ = 0;
            break;
          }
        } else overS_ = 0;
        if (!calm) { quietStart_ = -1; arrived_ = false; break; }
        if (quietStart_ < 0) { // The body may have stopped: remember the state now.
          quietStart_ = clock_;
          snap_ = {p_, v_, t_, q_};
          snapLive_ = horizontal(p_);
          slow_ = braked_;
          calmSum_ = {};
          calmN_ = 0;
        }
        // Gravity after the stop, averaged over the whole calm in the sensor
        // axes, each sample turned back to the stop by the gyro (a turntable
        // may already be turning).
        const Accel back = rotate(conj(mul(conj(snap_.q), q_)), a); // Sensor now -> sensor at the stop.
        calmSum_ = {calmSum_.x + back.x, calmSum_.y + back.y, calmSum_.z + back.z};
        ++calmN_;
        const float calmS = clock_ - quietStart_;
        // Carried (tilted, turned on the way, too long): set down and still
        // for `carrySettleS` is enough, then Wi-Fi finds the table.
        const bool carry = carrying();
        // Such a leg is over after a short calm even without braking: a slow
        // table is seen by the detector only half way, and waiting for the usual
        // parking time would swallow what follows (the way back, the next ride).
        const bool stopped = calmS >= (slow_ || vertLeg ? (vertLeg ? legStopS : stopS) : carry ? carrySettleS : forcedStopS);
        if (stopped && !arrived_) {
          arrived_ = true;
          arrivalStation_ = slow_ && known_ ? rideTarget(snap_.p) : station_;
        }
        if (stopped && calmS >= (carry ? carrySettleS : vertLeg ? legStopS : parkS)) finish();
        break;
      }
    }
  }

  // Wi-Fi: call when a scan wanted by `scanWanted()` has started / finished.
  // With config.csv only tables with `wifi_scan` true are scanned (and any
  // table while it is unknown).
  bool scanWanted() const { return scanWanted_; }
  void scanStarted() { scanWanted_ = false; }
  void applyScan(const Fingerprint &scan) {
    lastScan_ = scan;
    const int n = stations_;
    for (int s = 0; s < maxStations; ++s) score_[s] = s < n && learned.fp[s].count ? distance(scan, learned.fp[s]) : -1;
    if (scan.count == 0) return;
    // Wi-Fi only finds the table while it is unknown; a counted table is
    // never overruled (motion and the reference points own it).
    int best = -1;
    corrected_ = false;
    if (!known_ && lost_ == Lost::Carried) {
      // Just set down after being carried: the best match is the table now,
      // straight away (only scanned tables compete).
      for (int s = 0; s < n; ++s)
        if (score_[s] >= 0 && (best < 0 || score_[s] < score_[best])) best = s;
      if (best < 0) { relocateAt_ = clock_ + relocateRetryS; return; } // No table has a fingerprint yet.
      locate(best);
    } else if (!known_) {
      // Table from Wi-Fi alone (after boot or lost samples):
      // only a clear match counts; otherwise try again a little later. Scans
      // taken at the same spot are averaged, so close neighbours separate.
      ++scoreN_;
      for (int s = 0; s < n; ++s) scoreSum_[s] += score_[s] < 0 ? 0 : score_[s];
      best = -1;
      for (int s = 0; s < n; ++s)
        if (score_[s] >= 0 && (best < 0 || scoreSum_[s] < scoreSum_[best])) best = s;
      float second = 1e9f;
      for (int s = 0; s < n; ++s)
        if (s != best && score_[s] >= 0 && scoreSum_[s] / scoreN_ < second) second = scoreSum_[s] / scoreN_;
      const float bestAvg = best >= 0 ? scoreSum_[best] / scoreN_ : 1e9f;
      if (best < 0 || bestAvg > relocateMaxDb ||
          second - bestAvg < std::max(relocateMarginDb, relocateMarginShare * bestAvg)) {
        relocateAt_ = clock_ + relocateRetryS;
        return;
      }
      locate(best);
    }
    if (!known_) return;
    Fingerprint &ref = learned.fp[station_];
    if (ref.count == 0) blend(ref, scan, 1.0f); else blend(ref, scan, 0.3f);
    learnedChanged_ = true;
  }

  // State for display.
  Phase phase() const { return phase_; }
  int station() const { return station_; }
  bool known() const { return known_; }
  Lost lost() const { return known_ ? Lost::None : lost_; }
  // Being carried right now (tilted or turned on the way, or far too long).
  bool carrying() const {
    if (phase_ != Phase::Moving) return false;
    const Quat q = arrived_ || quietStart_ >= 0 ? snap_.q : q_;
    const Accel up = scale(rest_.g, 1 / std::max(norm(rest_.g), 1e-6f));
    const float yaw = 2 * std::atan2(dot({q.x, q.y, q.z}, up), q.w);
    return minUpCos_ < std::cos(carryTilt) || std::fabs(wrap(yaw)) > carryYaw || t_ > maxTransitS;
  }
  bool arrived() const { return phase_ == Phase::Moving && arrived_; }
  float phaseS() const { return phase_ == Phase::Moving && arrived_ ? clock_ - quietStart_ : clock_ - phaseStart_; }
  // Seconds until the stop counts as parked (and is scanned); <0 when not waiting.
  float untilPark() const {
    if (phase_ == Phase::Moving) return arrived_ ? std::max(0.0f, parkS - (clock_ - quietStart_)) : -1;
    if (phase_ == Phase::Parked && !parkDone_) return parkS - (clock_ - phaseStart_);
    return -1;
  }
  float liveDistance() const {
    if (phase_ != Phase::Moving) return 0;
    const Accel p = arrived_ ? snap_.p : p_;
    return std::max(horizontal(p), known_ && atShaft(station_) ? std::fabs(vertical(p)) : 0.0f);
  }
  // Position as a fractional table index, for the animation.
  float position() const {
    if (phase_ != Phase::Moving) return float(station_);
    if (arrived_) return float(arrivalStation_);
    const float d = liveDistance();
    const int dir = direction();
    float pos = station_ + dir * (d / gapAhead(dir));
    const float maxPos = float(stations_ - 1);
    return pos < 0 ? 0 : pos > maxPos ? maxPos : pos;
  }
  int direction() const {
    if (phase_ != Phase::Moving) return 0;
    if (const int s = shaftWay(vertical(p_)); s && std::fabs(vertical(p_)) > horizontal(p_)) return s;
    return direction(flat(p_));
  }
  // Gap i (table i to i+1) is a lift shaft: +1 up, -1 down; 0: on the floor.
  int shaft(int i) const { return i >= 0 && i < maxStations ? shaft_[i] : 0; }
  // Direction of gap i on the floor (rad, CCW, 0 = as the body at table 1), for
  // drawing: config.csv directions are the sensor's and turn with the body.
  float shownDir(int i) const { return shownDir_[i]; }
  // Gap i is ridden along the floor (has a direction).
  bool floorGap(int i) const { return i >= 0 && i < maxStations && gapFloor_[i]; }
  // Gap i is a turntable (RZ+/RZ-): +1 left (CCW), -1 right; gap(i) is its angle in degrees.
  int turntable(int i) const { return i >= 0 && i < maxStations ? turn_[i] : 0; }
  float gap(int i) const { return gap_[i] > 0 ? gap_[i] : 0; }
  // Largest RMS vibration (m/s2, high-passed) recorded on any ride that
  // arrived at station `s`, per axis (0=X,1=Y,2=Z). 0 until a ride arrives.
  float stationVib(int s, int axis) const {
    return s >= 0 && s < maxStations && axis >= 0 && axis < 3 ? stationVibMax_[s][axis] : 0;
  }
  const Fingerprint &lastScan() const { return lastScan_; }
  float score(int s) const { return score_[s]; }
  bool lastCorrected() const { return corrected_; }
  // Heading of the body on the floor (rad, CCW, 0 = as at table 1).
  float heading() const { return heading_; }
  bool headingKnown() const { return headingKnown_; }
  bool turning() const { return turning_; }
  float turnSoFar() const { return turning_ ? heading_ - turnFrom_ : 0; }
  float lastTurn() const { return lastTurn_; }   // rad, after snapping.
  float sinceTurnS() const { return turnCount_ ? clock_ - turnAt_ : -1; }
  bool newTurn() { const bool r = newTurn_; newTurn_ = false; return r; }

  // Shaking measured while standing, and the thresholds derived from it.
  float activity() const { return hv_; }           // Horizontal leaky velocity, m/s.
  float startThreshold() const { return clampf(startK * std::sqrt(levelV2_), minStartV, maxStartV); }
  float shaking() const { return std::sqrt(levelA2_); } // Low-passed horizontal acceleration RMS, m/s2.
  float stillThreshold() const { return clampf(stillK * std::sqrt(levelA2_), minStillA, maxStillA); }
  // Same for the vertical, used only next to a lift shaft.
  float shaftThreshold() const { return clampf(startK * std::sqrt(levelVz2_), minShaftV, maxShaftV); }
  // Vertical acceleration (low-passed) under which a technological lift leg is still.
  float stillVThreshold() const { return clampf(stillK * std::sqrt(levelVa2_), minStillA, maxStillA); }
  float verticalActivity() const { return vv_; } // Vertical leaky velocity, m/s (up +).

  // Transits, oldest first; `newTransit()` is true once per finished transit.
  int transitCount() const { return transitCount_; }
  const Transit &transit(int i) const { return transits_[(transitHead_ + maxTransits - transitCount_ + i) % maxTransits]; }
  bool newTransit() { const bool r = newTransit_; newTransit_ = false; return r; }
  // Log rows (distance log, see DistanceLog.h), queued: several may come
  // between two calls (a cycle closed and a ride logged at once, samples
  // processed in a batch). `takeDistanceRow()` hands them out oldest first.
  bool takeDistanceRow(const char *&file, const char *&csv) {
    if (!rowCount_) return false;
    const LogRow &r = rows_[(rowHead_ + rowQueue - rowCount_) % rowQueue];
    --rowCount_;
    file = r.file;
    csv = r.csv;
    return true;
  }
  int droppedRows() const { return droppedRows_; }
  // A reference point (a turn or a move in Z only some stations make) has
  // just set the station: true once; `refFixStation()` names it.
  bool newRefFix() { const bool r = newRefFix_; newRefFix_ = false; return r; }
  const char *refFixStation() const { return refFixShown_; }
  // The newest row (for display and checks): true once per new row.
  bool newDistanceRow() { const bool r = newDistanceRow_; newDistanceRow_ = false; return r; }
  const char *distanceRowCsv() const { return distanceRow_; }
  // File (without ".csv") the last row belongs in: "B2-B3" or "B3_tech".
  const char *distanceRowFile() const { return logName_; }
  bool learnedChanged() { const bool r = learnedChanged_; learnedChanged_ = false; return r; }

  void breakSignal() {
    primed_ = false;
    ringCount_ = head_ = 0;
    if (phase_ == Phase::Moving && known_) { known_ = false; lost_ = Lost::SamplesLost; } // Let Wi-Fi find the table.
    phase_ = Phase::Settling;
    arrived_ = false;
    refSet_ = false; // Gravity again from the next full ring; the shaking level and gyro offset are kept.
    turning_ = false;
    clearDetector();
  }

private:
  struct Sample { Accel a, w; float dt; };
  struct Rest { Accel g, bias; };
  struct Quat { float w, x, y, z; };
  struct Snap { Accel p, v; float t; Quat q; };

  Sample ring_[restSamples] = {};
  Rest hist_[restSamples] = {};    // Gravity and gyro offset at each ring slot's time.
  int head_ = 0, ringCount_ = 0;
  Rest rest_{};                    // During a ride: gravity and offset at its start.
  Phase phase_ = Phase::Settling;
  int station_ = 0, arrivalStation_ = 0;
  bool known_ = false, primed_ = false, arrived_ = false, parkDone_ = false;
  bool scanWanted_ = false, corrected_ = false, newTransit_ = false, learnedChanged_ = false;
  uint32_t lastUs_ = 0, nextId_ = 1, lastTransitId_ = 0;
  float clock_ = 0, phaseStart_ = 0, quietStart_ = -1, snapLive_ = 0, settleStart_ = 0;
  // Attitude while not riding: gravity in the sensor axes, gyro offset.
  Accel gB_{}, gMid_{}, bias_{}, biasMid_{}, wLp_{};
  bool refSet_ = false, biasSet_ = false;
  // Heading and turns.
  float gateOverS_ = 0;
  float heading_ = 0, rideHeading_ = 0, turnFrom_ = 0, lastTurn_ = 0, turnAt_ = 0, turnQuietS_ = 0, yawLp_ = 0;
  bool headingKnown_ = false, turning_ = false, newTurn_ = false;
  int turnCount_ = 0;
  // Ride detector.
  Accel lp1_{}, lp2_{}, vl_{}, vlFlat_{}, rideDir_{}, calmSum_{};
  Accel vlMean_{}, haMean_{};
  long calmN_ = 0;
  float ha_ = 0, hv_ = 0, overS_ = 0;
  float levelV2_ = 0, levelA2_ = 0;
  bool slow_ = false, braked_ = false;
  // Ride integration.
  Quat q_{1, 0, 0, 0};
  Accel prevW_{}, prevAcc_{}, v_{}, p_{}, lp_{};
  float t_ = 0, rideRate_ = 0; // rideRate_: gyro rate (minus offset), 0.5 s mean.
  float levelW2_ = 0;          // Mean square gyro rate standing: the structure rocking.
  float tiltLp_ = 0;           // Rate about a horizontal axis, 0.3 s mean: a hand tilts, a turntable does not.
  float yawFast_ = 0;          // Rate about the vertical, 0.1 s mean: a turntable's start and stop, quickly.
  int sinceTurn_ = restSamples; // Samples since the body last turned level (Parked), up to a ring.
  Accel aFast_{}, aSlow_{};    // Raw acceleration, 0.2 s and 1 s means (sensor axes), for a carry's end.
  bool first_ = true;
  Snap snap_{};
  double sq_[3] = {};
  float peak_[3] = {}, ramp_ = 0;
  long n_ = 0;
  int16_t workLo_[workBins][3], workHi_[workBins][3];
  int work_ = 0, perBin_ = 1;   // perBin_: work bins merged per stored bin (doubles when full).
  float binStart_ = 0;
  bool binOpen_ = false;
  Fingerprint lastScan_{};
  float score_[maxStations] = {};
  float stationVibMax_[maxStations][3] = {};
  bool configAuthoritative_ = false; // A valid config.csv was applied; without one nothing is tracked.
  int stations_ = 2;
  float gap_[maxStations] = {};      // config.csv: metres from table i to i+1 (shaft: height; turntable: degrees).
  float gapDir_[maxStations] = {};   // config.csv: direction of gap i in the sensor's axes, rad.
  bool gapFloor_[maxStations] = {};  // Gap i is ridden along the floor (not a shaft or a turntable).
  StationRow stationRow_[maxStations];
  int8_t shaft_[maxStations] = {}; // From config.csv only, see shaft().
  int8_t turn_[maxStations] = {};  // From config.csv only, see turntable().
  float shownDir_[maxStations] = {}; // config.csv gap directions turned onto the floor, for drawing.
  int pendDir_ = 0;                // Open RPS_seat cycle: way out (+1 up / -1 down); 0: none.
  float pendDepth_ = 0, pendAt_ = 0;
  int pendStation_ = -1;
  char pendFix_[48] = {};          // The reference fix that came with the way out, if any.
  char refFix_[48] = {};           // "ref_fix=A>B" while the move that caused it is logged.
  char refFixShown_[16] = {};      // Station of the last reference fix, for display.
  bool newRefFix_ = false;
  int rideUp_ = 0;                 // This ride started up (+1) / down (-1) a shaft; 0: along the floor.
  float vv_ = 0, vvMean_ = 0, levelVz2_ = 0;
  float va_ = 0, vaMean_ = 0, levelVa2_ = 0; // Low-passed vertical acceleration, its 1 s mean and shaking level.
  Accel vlLeg_{};                  // Horizontal leaky velocity when a move in Z started.
  bool legThenRide_ = false;       // This ride began as a move in Z (see Phase::Moving).
  static float directionAngle(Direction d) {
    switch (d) {
      case Direction::XPlus: return 0;
      case Direction::XMinus: return pi;
      case Direction::YPlus: return pi / 2;
      case Direction::YMinus: return -pi / 2;
      case Direction::ZPlus:
      case Direction::ZMinus:
      case Direction::RZPlus:
      case Direction::RZMinus: return 0; // A shaft or a turntable: no floor direction.
    }
    return 0;
  }
  Transit transits_[maxTransits];
  int transitHead_ = 0, transitCount_ = 0;
  char distanceRow_[256] = {};
  char logName_[32] = {};
  // Name of station `s`: its config.csv id, else its number from 1.
  const char *stationName(int s) const {
    static char number[2][8];
    static int next = 0;
    if (s < 0) return "nieznana";
    if (const StationRow *row = stationRow(s); row && row->id[0]) return row->id;
    char *out = number[next++ & 1];
    std::snprintf(out, sizeof number[0], "%d", s + 1);
    return out;
  }
  void setLogName(bool technological, int from, int to) {
    logFileName(logName_, sizeof logName_, technological, from, to, [this](int s) { return stationName(s); });
  }
  bool newDistanceRow_ = false;
  static constexpr int rowQueue = 8;
  struct LogRow { char csv[256]; char file[32]; };
  LogRow rows_[rowQueue];
  int rowHead_ = 0, rowCount_ = 0, droppedRows_ = 0;
  // The row just formatted in distanceRow_ / logName_ goes into the queue.
  void pushRow() {
    LogRow &r = rows_[rowHead_];
    std::snprintf(r.csv, sizeof r.csv, "%s", distanceRow_);
    std::snprintf(r.file, sizeof r.file, "%s", logName_);
    rowHead_ = (rowHead_ + 1) % rowQueue;
    if (rowCount_ < rowQueue) ++rowCount_; else ++droppedRows_; // Oldest overwritten.
    newDistanceRow_ = true;
  }
  float lastDistance_ = 0;
  Lost lost_ = Lost::Boot;
  float relocateAt_ = -1;   // When to scan again while the table is unknown; <0: not planned.
  float minUpCos_ = 1;      // Most tilt during the move (cos of the angle), up to the stop.
  float scoreSum_[maxStations] = {}; // Wi-Fi scores summed over scans at one spot while lost.
  int scoreN_ = 0;
  void clearScores() { for (float &v : scoreSum_) v = 0; scoreN_ = 0; }
  // The table is `best`, found by Wi-Fi alone.
  void locate(int best) {
    station_ = best;
    known_ = true;
    corrected_ = true;
    lost_ = Lost::None;
    relocateAt_ = -1;
    clearScores();
    // Heading from the last stand there, if the turn since boot is unknown.
    if (!headingKnown_ && learned.headingKnown[best]) { heading_ = learned.heading[best]; headingKnown_ = true; }
    Transit *last = transitCount_ ? &transits_[(transitHead_ + maxTransits - 1) % maxTransits] : nullptr;
    if (last && last->id == lastTransitId_ && last->to < 0) { last->to = best; last->wifiCorrected = true; }
  }
  void wantScan() {
    const StationRow *row = known_ ? stationRow(station_) : nullptr;
    if (!row || row->wifiScan) scanWanted_ = true;
  }

  static float get(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }
  static float &ref(Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }
  static Accel sub(Accel a, Accel b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
  static Accel scale(Accel a, float k) { return {a.x * k, a.y * k, a.z * k}; }
  static float dot(Accel a, Accel b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static Accel cross(Accel a, Accel b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
  static float norm(Accel a) { return std::sqrt(dot(a, a)); }
  static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
  static Accel mix(Accel a, Accel b, float k) { return {a.x + k * (b.x - a.x), a.y + k * (b.y - a.y), a.z + k * (b.z - a.z)}; }
  static Quat conj(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }
  static Quat mul(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
  }
  static Accel rotate(Quat q, Accel v) {
    const Quat r = mul(mul(q, {0, v.x, v.y, v.z}), conj(q));
    return {r.x, r.y, r.z};
  }
  // Part of v across g (horizontal when g is gravity).
  static Accel flatten(Accel v, Accel g) {
    const float g2 = dot(g, g);
    return g2 > 0 ? sub(v, scale(g, dot(v, g) / g2)) : v;
  }
  Accel mean(Accel Sample::*f) const {
    Accel m{};
    for (const Sample &s : ring_) { m.x += (s.*f).x; m.y += (s.*f).y; m.z += (s.*f).z; }
    return {m.x / restSamples, m.y / restSamples, m.z / restSamples};
  }
  // Gravity in the sensor axes now.
  Accel gravityNow() const { return phase_ == Phase::Moving ? rotate(conj(q_), rest_.g) : gB_; }
  Accel biasNow() const { return phase_ == Phase::Moving ? rest_.bias : bias_; }

  // Gyro: turn the expected gravity with the sensor, follow the heading,
  // spot turns. Standing, the accelerometer pulls gravity back slowly and the
  // gyro offset is learned whenever the body is not turning.
  void attitude(Accel a, Accel w, float dt) {
    const Accel rate = sub(w, biasNow());
    const Accel g = gravityNow();
    const float gn = norm(g);
    const float yaw = gn > 0 ? dot(rate, g) / gn : 0; // About the vertical, CCW from above.
    heading_ += yaw * dt;
    // Turns while standing (turntables), also in the calm after a ride.
    const float kw = dt / (0.3f + dt);
    yawLp_ += kw * (yaw - yawLp_);
    yawFast_ += dt / (0.1f + dt) * (yaw - yawFast_);
    const float tiltRate = gn > 0 ? norm(sub(rate, scale(g, yaw / gn))) : 0; // About a horizontal axis.
    tiltLp_ += kw * (tiltRate - tiltLp_);
    if (phase_ != Phase::Moving || arrived_) {
      if (!turning_ && std::fabs(yawLp_) > turnStart) {
        turning_ = true;
        turnFrom_ = heading_ - yawLp_ * 0.3f; // The low-pass lagged ~0.3 s.
        turnQuietS_ = 0;
      } else if (turning_) {
        turnQuietS_ = std::fabs(yawLp_) < turnEnd ? turnQuietS_ + dt : 0;
        if (turnQuietS_ >= turnEndS) endTurn();
      }
    }
    if (phase_ == Phase::Moving) return; // The ride has its own attitude (q_).
    if (!turning_ && std::fabs(yawLp_) < 0.5f * turnStart && tiltLp_ < turntableTilt) {
      // Rocking of the structure (gyro), for telling a carry's end; learned
      // only while nothing turns or tilts the body.
      const float kl = dt / (levelTauS + dt), r = norm(rate);
      levelW2_ += kl * (r * r - levelW2_);
    }
    // A vector fixed on the floor turns the other way in the sensor axes.
    gB_ = sub(gB_, scale(cross(rate, gB_), dt));
    gMid_ = sub(gMid_, scale(cross(rate, gMid_), dt));
    const float kg = dt / (gravityTauS + dt);
    gMid_ = mix(gMid_, a, kg);
    gB_ = mix(gB_, gMid_, kg);
    // Slow part of the rate (1 s): shaking averages out, a turn or a tilt
    // (lifting table) does not. A "turn" lasting far longer than any table
    // does is the offset itself having moved (temperature): learn it then.
    wLp_ = mix(wLp_, rate, dt / (1.0f + dt));
    gateOverS_ = norm(wLp_) < gyroGate ? 0 : gateOverS_ + dt;
    if ((norm(wLp_) < gyroGate && !turning_) || gateOverS_ > gateGiveUpS) {
      const float kb = dt / (biasTauS + dt);
      biasMid_ = mix(biasMid_, w, kb);
      bias_ = mix(bias_, biasMid_, kb);
    }
  }
  void endTurn() {
    turning_ = false;
    const float turned = heading_ - turnFrom_;
    if (std::fabs(turned) < minTurn) return;
    const float snapped = std::round(turned / (pi / 2)) * (pi / 2);
    if (std::fabs(turned - snapped) < snapTol) heading_ = turnFrom_ + snapped; // The heading only: logs keep the measure.
    lastTurn_ = heading_ - turnFrom_;
    turnAt_ = clock_;
    ++turnCount_;
    newTurn_ = true;
    // As measured (not snapped), about Z as shown (down): right +; and in
    // quarter turns, as every turn on the line is configured.
    const float measuredDeg = -turned * 180 / pi;
    const int quarters = int(std::lround(measuredDeg / 90));
    refFix_[0] = 0;
    // Standing, a turn is a reference point: it says where the body is.
    if (phase_ == Phase::Parked && quarters)
      moveToReference(nearestReference([&](int st) { return turntableWayAt(st, quarters) || rotationAt(st, quarters); },
                                       known_ && (turntableWayAt(station_, quarters) || rotationAt(station_, quarters))));
    // A turntable between two positions (RZ+/RZ- in config.csv): the body is
    // at the next position (or back at the previous one when turned the
    // other way).
    if (const int way = known_ && phase_ == Phase::Parked ? turntableWayAt(station_, quarters) : 0) {
      if (pendDir_) flushLiftCycle("no_return");
      const int from = station_, i = way > 0 ? from : from - 1;
      station_ += way;
      formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000), "transport", from, station_, "",
                        Motion::None, "RZ", 0, measuredDeg, 1.0f, refFix_, float(way * turnDeg(i)));
      setLogName(false, from, station_);
      pushRow();
      if (headingKnown_) { learned.heading[station_] = heading_; learned.headingKnown[station_] = true; }
      learnedChanged_ = true;
      refFix_[0] = 0;
      enterPark(); // At the new position: its own stop, Wi-Fi scan and all.
      return;
    }
    // A turntable at the station (rotation_z): the same station. A turn
    // finished while parked never reaches finish(), so it is logged here.
    if (known_) {
      if (const StationRow *row = stationRow(station_); row && row->motion == Motion::RotationZ) {
        const MotionResult motionResult = classifyMotion(*row, 0, measuredDeg);
        char flags[64] = {};
        if (motionResult.overRange) addFlag(flags, sizeof flags, "over_range");
        if (refFix_[0]) addFlag(flags, sizeof flags, refFix_);
        formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000), "technological", -1, -1,
                          row->id, Motion::RotationZ, "Z", 0, measuredDeg, 1.0f, flags, row->motionRange); // Signed like the measure.
        setLogName(true, station_, station_);
        pushRow();
      }
    }
    refFix_[0] = 0;
  }

  // Ride detector signals, in the sensor axes now.
  void detect(Accel a, float dt) {
    const Accel g = gravityNow();
    const Accel dyn = sub(a, g);
    const float k = dt / (lpTauS + dt);
    for (int i = 0; i < 3; ++i) {
      ref(lp1_, i) += k * (get(dyn, i) - get(lp1_, i));
      ref(lp2_, i) += k * (get(lp1_, i) - get(lp2_, i));
      ref(vl_, i) += get(dyn, i) * dt - get(vl_, i) * dt / leakTauS;
    }
    ha_ = norm(flatten(lp2_, g));
    vlFlat_ = flatten(vl_, g);
    hv_ = norm(vlFlat_);
    // Vertical leaky velocity (up +), for lift shafts: a longer leak, as an
    // elevator starts more gently than the rollers.
    const float gn = norm(g), up = gn > 0 ? dot(dyn, g) / gn : 0;
    vv_ += up * dt - vv_ * dt / shaftLeakTauS;
    va_ = gn > 0 ? dot(lp2_, g) / gn : 0; // Low-passed vertical acceleration (up +).
  }
  // Standing: measure the shaking. Only the fluctuation counts: a constant
  // offset (a gravity estimate still settling) must not raise the thresholds.
  void follow(float dt, float levelTau) {
    const float kl = dt / (levelTau + dt);
    const Accel g = gB_;
    const Accel haVec = flatten(lp2_, g);
    vlMean_ = mix(vlMean_, vlFlat_, kl);
    haMean_ = mix(haMean_, haVec, kl);
    const float dv = norm(sub(vlFlat_, vlMean_)), da = norm(sub(haVec, haMean_));
    levelV2_ += kl * (dv * dv - levelV2_);
    levelA2_ += kl * (da * da - levelA2_);
    // Shaking only: around a 1 s mean, so gravity still settling after a
    // ride (slow, and integrated over the longer leak) is left out.
    vvMean_ += dt / (1.0f + dt) * (vv_ - vvMean_);
    levelVz2_ += kl * ((vv_ - vvMean_) * (vv_ - vvMean_) - levelVz2_);
    vaMean_ += dt / (1.0f + dt) * (va_ - vaMean_);
    levelVa2_ += kl * ((va_ - vaMean_) * (va_ - vaMean_) - levelVa2_);
    hist_[(head_ + restSamples - 1) % restSamples] = {gB_, bias_}; // Slot of the sample just added.
  }
  void clearDetector() { lp1_ = lp2_ = vl_ = vlFlat_ = vlMean_ = haMean_ = {}; ha_ = hv_ = overS_ = vv_ = vvMean_ = va_ = vaMean_ = 0; }
  void abandon() {
    pendDir_ = 0;
    if (known_) { known_ = false; lost_ = Lost::Carried; }
    phase_ = Phase::Settling;
    arrived_ = false;
    refSet_ = false; // Gravity again from the next full ring (the gyro offset stays).
    ringCount_ = head_ = 0;
    turning_ = false;
    clearDetector();
  }
  void enterPark() {
    phase_ = Phase::Parked;
    phaseStart_ = clock_;
    parkDone_ = false;
    arrived_ = false;
  }

  // Horizontal part of a vector given in the axes at the ride start.
  Accel flat(Accel p) const { return flatten(p, rest_.g); }
  float horizontal(Accel p) const { return norm(flat(p)); }
  // Vertical part (up +) of a vector given in the axes at the ride start.
  float vertical(Accel p) const { return dot(p, rest_.g) / std::max(norm(rest_.g), 1e-6f); }
  // Signed angle (deg, about Z as shown: right +) of turntable gap i, ridden
  // forward (from table i to i+1); 0 when gap i is not a turntable.
  float turnDeg(int i) const { return i >= 0 && i < maxStations ? -turn_[i] * gap_[i] : 0; }
  // Along the line (+1 / -1) a turn of `quarters` (x 90 deg, right +) takes
  // through a turntable next to station `s`; 0 when none there turns so.
  int turntableWayAt(int s, int quarters) const {
    if (!quarters) return 0;
    const auto is = [&](float deg) { return deg != 0 && std::lround(deg / 90) == quarters; };
    if (s < stations_ - 1 && is(turnDeg(s))) return 1;
    if (s > 0 && is(-turnDeg(s - 1))) return -1;
    return 0;
  }
  // Station `s` turns the body by `quarters` (rotation_z).
  bool rotationAt(int s, int quarters) const {
    const StationRow *r = stationRow(s);
    return quarters && r && r->motion == Motion::RotationZ && std::lround(r->motionRange / 90) == quarters;
  }
  // Reference points: a turn or a move in Z that the counted station cannot
  // explain (`explained` false) puts the body at the nearest station that
  // can (`ok`), if exactly one is nearest; while the station is unknown, at
  // the only one on the line. -1: no change.
  template <class Ok> int nearestReference(Ok ok, bool explained) const {
    if (explained) return -1;
    int best = -1, bestD = 1 << 30;
    bool tie = false;
    for (int st = 0; st < stations_; ++st) {
      if (!ok(st)) continue;
      const int d = known_ ? std::abs(st - station_) : 0;
      if (d < bestD) { best = st; bestD = d; tie = false; }
      else if (d == bestD) tie = true;
    }
    return tie ? -1 : best;
  }
  void moveToReference(int st) {
    if (st < 0 || (known_ && st == station_)) return;
    std::snprintf(refFix_, sizeof refFix_, "ref_fix=%s>%s", known_ ? stationName(station_) : "nieznana", stationName(st));
    std::snprintf(refFixShown_, sizeof refFixShown_, "%s", stationName(st));
    station_ = st;
    known_ = true;
    lost_ = Lost::None;
    relocateAt_ = -1;
    clearScores();
    newRefFix_ = true;
  }
  // A move in Z (up +) the counted station cannot explain (no shaft that
  // way, no technological move in Z there): see nearestReference().
  void resolveVertical(float vz) {
    const float h = std::fabs(vz);
    const int up = vz > 0 ? 1 : -1;
    const auto ok = [&](int st) {
      if (const StationRow *r = stationRow(st); r && (r->motion == Motion::Lift || r->motion == Motion::RpsSeat) &&
                                                  h >= std::max(minLegM, underRangeK * r->motionRange))
        return true;
      if (st < stations_ - 1 && shaft_[st] == up && h >= nudgeShare * gap_[st]) return true; // Up or down to st + 1.
      return st > 0 && shaft_[st - 1] == -up && h >= nudgeShare * gap_[st - 1];              // Back to st - 1.
    };
    moveToReference(nearestReference(ok, known_ && (shaftTarget(vz) != station_ || techLift(station_))));
  }
  // Distance config.csv gives between tables `a` and `b` (sum of the gaps
  // between them).
  float declaredSpan(int a, int b) const {
    if (a == b) return 0;
    float sum = 0;
    for (int i = std::min(a, b); i < std::max(a, b) && i < maxStations; ++i) sum += gap_[i];
    return sum;
  }
  // Station `s` moves the body in Z (lift or RPS_seat).
  bool techLift(int s) const {
    const StationRow *r = stationRow(s);
    return r && (r->motion == Motion::Lift || r->motion == Motion::RpsSeat);
  }
  static void addFlag(char *flags, size_t size, const char *flag) {
    const size_t n = std::strlen(flags);
    std::snprintf(flags + n, size - n, "%s%s", n ? ";" : "", flag);
  }
  static bool underRange(const StationRow &row, float depth) {
    return row.motionRange != 0 && depth < underRangeK * std::fabs(row.motionRange);
  }
  void logLift(int s, float depth, const char *flags) {
    const StationRow *row = stationRow(s);
    formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000), "technological", -1, -1,
                      row ? row->id : "", row ? row->motion : Motion::Lift, "Z", depth, 0, 1.0f, flags,
                      row ? std::fabs(row->motionRange) : 0);
    setLogName(true, s, s);
    pushRow();
  }
  // The open down-and-back cycle never got its way back.
  void flushLiftCycle(const char *flag) {
    char flags[96] = {};
    if (const StationRow *row = stationRow(pendStation_)) {
      if (classifyMotion(*row, pendDepth_, 0).overRange) addFlag(flags, sizeof flags, "over_range");
      if (underRange(*row, pendDepth_)) addFlag(flags, sizeof flags, "under_range");
    }
    addFlag(flags, sizeof flags, flag);
    if (pendFix_[0]) addFlag(flags, sizeof flags, pendFix_);
    logLift(pendStation_, pendDepth_, flags);
    pendDir_ = 0;
  }
  // One move in Z (`vz`, up +) at a station with a technological move in Z.
  // lift: one move, either way, logged at once. RPS_seat: down onto the
  // pins and back up (or up and back down), either way first; logged once
  // the way back closes the cycle, with the way back in back_m.
  void liftLeg(const StationRow &row, float vz) {
    const int dir = vz > 0 ? 1 : -1;
    const float depth = std::fabs(vz);
    char flags[96] = {};
    if (row.motion == Motion::RpsSeat && pendDir_ && pendStation_ == station_ && dir == -pendDir_) {
      if (classifyMotion(row, pendDepth_, 0).overRange) addFlag(flags, sizeof flags, "over_range");
      if (underRange(row, pendDepth_)) addFlag(flags, sizeof flags, "under_range");
      if (std::fabs(depth - pendDepth_) > std::max(returnTol * pendDepth_, returnTolM)) addFlag(flags, sizeof flags, "return_mismatch");
      char back[24];
      std::snprintf(back, sizeof back, "back_m=%.3f", depth); // The way back, measured too.
      addFlag(flags, sizeof flags, back);
      if (pendFix_[0]) addFlag(flags, sizeof flags, pendFix_);
      if (refFix_[0]) addFlag(flags, sizeof flags, refFix_);
      logLift(station_, pendDepth_, flags);
      pendDir_ = 0;
      return;
    }
    if (pendDir_) flushLiftCycle("no_return"); // The same way again, or another station.
    if (row.motion == Motion::RpsSeat) { // The way out: logged when the way back is done.
      pendDir_ = dir;
      pendDepth_ = depth;
      pendAt_ = clock_;
      pendStation_ = station_;
      std::snprintf(pendFix_, sizeof pendFix_, "%s", refFix_);
      return;
    }
    if (classifyMotion(row, depth, 0).overRange) addFlag(flags, sizeof flags, "over_range");
    if (underRange(row, depth)) addFlag(flags, sizeof flags, "under_range");
    if (refFix_[0]) addFlag(flags, sizeof flags, refFix_);
    logLift(station_, depth, flags);
  }
  bool atShaft(int s) const { return (s < stations_ - 1 && shaft_[s]) || (s > 0 && shaft_[s - 1]); }
  // Along the line (+1 / -1) a vertical move `vz` takes through a shaft
  // next to the current table; 0 when no shaft there goes that way.
  int shaftWay(float vz) const {
    const int up = vz > 0 ? 1 : -1, s = station_;
    if (!known_) return 0;
    if (s < stations_ - 1 && shaft_[s] == up) return 1;
    if (s > 0 && shaft_[s - 1] == -up) return -1;
    return 0;
  }
  // Stop reached by a vertical move: shaft by shaft, rounded like targetFor().
  int shaftTarget(float vz) const {
    const int dir = shaftWay(vz);
    if (!dir) return station_;
    const int up = vz > 0 ? 1 : -1;
    int s = station_;
    float left = std::fabs(vz);
    for (;;) {
      const int i = dir > 0 ? s : s - 1;
      if (i < 0 || i >= stations_ - 1 || shaft_[i] != dir * up) break;
      const float g = gap_[i] > 0 ? gap_[i] : 1.0f;
      if (left < (s == station_ ? nudgeShare * g : g / 2)) break;
      left -= g;
      s += dir;
    }
    return s;
  }
  // Table a ride ending at `p` (axes at the start) reached: through a shaft
  // when it went mostly up or down next to one, else along the floor.
  int rideTarget(Accel p) const {
    const float vz = vertical(p), d = horizontal(p);
    const int lifted = shaftTarget(vz);
    if (lifted != station_ && std::fabs(vz) > d) return lifted;
    return targetFor(flat(p), d);
  }
  // Direction on the floor of a horizontal vector in the axes at the ride
  // start: its angle there (from the sensor's X turned flat, CCW about up)
  // plus the heading the ride started with.
  float floorAngle(Accel ph) const {
    const Accel up = scale(rest_.g, 1 / std::max(norm(rest_.g), 1e-6f));
    Accel e1 = flatten({1, 0, 0}, up);
    if (norm(e1) < 0.3f) e1 = flatten({0, 1, 0}, up);
    e1 = scale(e1, 1 / norm(e1));
    const Accel e2 = cross(up, e1);
    return wrap(rideHeading_ + std::atan2(dot(ph, e2), dot(ph, e1)));
  }
  // Forward (+1) or back (-1) along the line, from the configured directions
  // of the floor gaps next to the current table; forward when there are none.
  int direction(Accel ph) const {
    // The gaps' directions are in the sensor's axes (the body turns with it),
    // so the ride's own direction in them counts and the heading does not.
    if (norm(ph) < 0.05f) return 1;
    const float ang = wrap(floorAngle(ph) - rideHeading_);
    const int s = station_;
    const bool f = s < stations_ - 1 && gapFloor_[s];
    const bool b = s > 0 && gapFloor_[s - 1];
    const float cf = f ? std::cos(ang - gapDir_[s]) : 0;
    const float cb = b ? std::cos(ang - gapDir_[s - 1] - pi) : 0;
    if (f && b) return cf >= cb ? 1 : -1;
    if (f) return cf > -0.5f ? 1 : -1;
    if (b) return cb > 0.5f ? -1 : 1;
    return s == stations_ - 1 ? -1 : 1;
  }
  float gapAhead(int dir) const {
    const int i = dir > 0 ? station_ : station_ - 1;
    return i >= 0 && i < maxStations && gap_[i] > 0 ? gap_[i] : 1.0f;
  }
  // Table reached after `d` metres: walk gap by gap with the configured spacings
  // (rounded to the nearest table); a short nudge stays at the same table.
  int targetFor(Accel ph, float d) const {
    const int dir = direction(ph);
    if (d < minTransitM || d < nudgeShare * gapAhead(dir)) return station_;
    int s = station_;
    float left = d;
    for (;;) {
      const int i = dir > 0 ? s : s - 1;
      if (i < 0 || i >= stations_ - 1 || shaft_[i] || turn_[i]) break; // Not ridden along the floor.
      const float g = gap_[i] > 0 ? gap_[i] : 1.0f;
      if (s != station_ && left < g / 2) break;
      left -= g;
      s += dir;
      if (left < g / 2) break;
    }
    return s;
  }

  void startMove() {
    // Gravity and gyro offset from the oldest ring slot: from before the
    // pre-roll, so the start of the ride has not leaked into them yet.
    // Right after a turntable the pre-roll starts where the turn stopped: the
    // arc before it is no part of the ride.
    const int skip = std::max(0, restSamples - sinceTurn_);
    rest_ = hist_[(head_ + skip) % restSamples];
    // Heading at the start of the pre-roll: take back what the pre-roll turned.
    rideHeading_ = heading_;
    // The way the ride started (leaky velocity), to recognise its braking.
    const float n = norm(vlFlat_);
    rideDir_ = n > 1e-6f ? scale(vlFlat_, 1 / n) : Accel{};
    rideUp_ = hv_ <= startThreshold() ? (vv_ > 0 ? 1 : -1) : 0; // Started by the vertical.
    vlLeg_ = vlFlat_;
    legThenRide_ = false;
    braked_ = false;
    minUpCos_ = 1;
    clearScores(); // A new spot: earlier scans no longer apply.
    phase_ = Phase::Moving;
    arrived_ = false;
    quietStart_ = -1;
    overS_ = 0;
    turning_ = false;
    q_ = {1, 0, 0, 0};
    v_ = p_ = lp_ = {};
    t_ = rideRate_ = 0;
    first_ = true;
    for (double &s : sq_) s = 0;
    for (float &p : peak_) p = 0;
    ramp_ = 0;
    n_ = 0;
    work_ = 0;
    perBin_ = 1;
    binOpen_ = false;
    for (int i = skip; i < restSamples; ++i) {
      const Sample &s = ring_[(head_ + i) % restSamples];
      step(s.a, s.w, i > skip ? s.dt : 0);
    }
    // The pre-roll's own turn was already in the heading.
    const float yawPre = 2 * std::atan2(dot({q_.x, q_.y, q_.z}, rest_.g) / std::max(norm(rest_.g), 1e-6f), q_.w);
    rideHeading_ -= yawPre;
  }
  void step(Accel a, Accel w, float dt) {
    if (first_) dt = 0;
    if (!first_) {
      const float rx = 0.5f * (w.x + prevW_.x) - rest_.bias.x, ry = 0.5f * (w.y + prevW_.y) - rest_.bias.y,
                  rz = 0.5f * (w.z + prevW_.z) - rest_.bias.z;
      const float rate = std::sqrt(rx * rx + ry * ry + rz * rz), angle = rate * dt;
      if (angle > 1e-9f) {
        const float s = std::sin(angle / 2) / rate;
        q_ = mul(q_, {std::cos(angle / 2), rx * s, ry * s, rz * s});
        const float n = std::sqrt(q_.w * q_.w + q_.x * q_.x + q_.y * q_.y + q_.z * q_.z);
        q_ = {q_.w / n, q_.x / n, q_.y / n, q_.z / n};
      }
    }
    prevW_ = w;
    if (!first_) {
      rideRate_ += dt / (0.5f + dt) * (norm(sub(w, rest_.bias)) - rideRate_);
      aFast_ = mix(aFast_, a, dt / (0.2f + dt));
      aSlow_ = mix(aSlow_, a, dt / (1.0f + dt));
    } else aFast_ = aSlow_ = a;
    if (quietStart_ < 0) { // Tilt so far: where the start's up axis has gone.
      const Accel up = scale(rest_.g, 1 / std::max(norm(rest_.g), 1e-6f));
      minUpCos_ = std::min(minUpCos_, dot(up, rotate(q_, up)));
    }
    const Accel acc = sub(rotate(q_, a), rest_.g);
    if (!first_) {
      for (int i = 0; i < 3; ++i) {
        const float vOld = get(v_, i);
        ref(v_, i) += 0.5f * (get(acc, i) + get(prevAcc_, i)) * dt;
        ref(p_, i) += 0.5f * (vOld + get(v_, i)) * dt;
        ref(lp_, i) += dt / (vibTauS + dt) * (get(acc, i) - get(lp_, i));
      }
    } else lp_ = acc;
    prevAcc_ = acc;
    t_ += dt;
    first_ = false;
    // Vibration: what the smoothed (ramp) acceleration does not explain.
    for (int i = 0; i < 3; ++i) {
      const float vib = get(acc, i) - get(lp_, i);
      sq_[i] += double(vib) * vib;
      if (std::fabs(vib) > peak_[i]) peak_[i] = std::fabs(vib);
    }
    ++n_;
    const float r = norm(flat(lp_));
    if (r > ramp_) ramp_ = r;
    record(acc);
  }
  // Envelope of the acceleration without gravity, merged 2:1 when full.
  void record(Accel acc) {
    const float binS = workBinS * perBin_;
    if (!binOpen_ || t_ - binStart_ >= binS) {
      if (work_ == workBins) {
        for (int k = 0; k < workBins / 2; ++k)
          for (int i = 0; i < 3; ++i) {
            workLo_[k][i] = std::min(workLo_[2 * k][i], workLo_[2 * k + 1][i]);
            workHi_[k][i] = std::max(workHi_[2 * k][i], workHi_[2 * k + 1][i]);
          }
        work_ = workBins / 2;
        perBin_ *= 2;
      }
      binStart_ = binOpen_ ? binStart_ + binS : t_;
      binOpen_ = true;
      for (int i = 0; i < 3; ++i) workLo_[work_][i] = 32767, workHi_[work_][i] = -32768;
      ++work_;
    }
    for (int i = 0; i < 3; ++i) {
      const float c = get(acc, i) * 100;
      const int16_t v = int16_t(c > 32767 ? 32767 : c < -32768 ? -32768 : c);
      int16_t &lo = workLo_[work_ - 1][i], &hi = workHi_[work_ - 1][i];
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
  }

  void finish() {
    // State when the body stopped; gravity after it, in the axes at the start.
    const float T = snap_.t;
    const Accel calm = calmN_ ? scale(calmSum_, 1.0f / calmN_) : mean(&Sample::a); // Sensor axes at the stop.
    const Accel g1 = rotate(snap_.q, calm);
    const Accel dg = sub(g1, rest_.g);
    Accel p = snap_.p, v = snap_.v;
    for (int i = 0; i < 3; ++i) {
      // The gyro's error grows gravity's error about linearly in time.
      ref(v, i) -= get(dg, i) * T / 2;
      ref(p, i) -= get(dg, i) * T * T / 6;
      // Whatever velocity is left at the stop is drift, also ~linear.
      ref(p, i) -= get(v, i) * T / 2;
    }
    const Accel ph = flat(p);
    const float vz = vertical(p);
    // Carried elsewhere rather than ridden: tilted, turned on the way, far
    // longer than a ride, or a long move without braking. Distance and
    // direction mean nothing then; the table comes from Wi-Fi alone.
    const Accel up = scale(rest_.g, 1 / std::max(norm(rest_.g), 1e-6f));
    const float yawToStop = 2 * std::atan2(dot({snap_.q.x, snap_.q.y, snap_.q.z}, up), snap_.q.w);
    const bool level = minUpCos_ >= std::cos(carryTilt) && std::fabs(wrap(yawToStop)) <= carryYaw && T <= maxTransitS;
    // Mostly up or down and level: a lift shaft or a technological move in
    // Z; one the counted station cannot explain is a reference point.
    const bool upright = level && std::fabs(vz) > norm(ph) && std::fabs(vz) >= minLegM;
    refFix_[0] = 0;
    if (upright) resolveVertical(vz);
    const int lifted = upright ? shaftTarget(vz) : station_;
    const bool shaftRide = known_ && lifted != station_; // Up or down a lift shaft.
    const float d = shaftRide ? std::fabs(vz) : norm(ph);
    const float floorDir = shaftRide ? 0 : floorAngle(ph);
    const int from = station_;
    const bool knownBefore = known_;
    const bool moved = shaftRide || d >= minTransitM; // shaftTarget() already left out a nudge.
    // An elevator seen only half way (no braking) is still one: it is upright.
    const bool carried = moved && (!level || (!slow_ && !shaftRide));
    if (carried && pendDir_) flushLiftCycle("no_return");
    if (carried && known_) { known_ = false; lost_ = Lost::Carried; }
    const bool ride = moved && (slow_ || shaftRide) && !carried;
    if (upright && !shaftRide && known_ && techLift(station_)) liftLeg(*stationRow(station_), vz);
    // A move in Z that a ride followed at once: the move is the vertical part.
    if (legThenRide_ && !upright && level && known_ && techLift(station_) && std::fabs(vz) >= minLegM)
      liftLeg(*stationRow(station_), vz);
    // `targetFor()` resolves a short ride to the *same* table (a "nudge",
    // see its comment): a maneuver at the current table (z_transfer,
    // compound) lands on `to == from` and is classified as technological
    // motion, not logged as a same-table "transport" ride. This check must
    // run, and `maneuver` must be known, before `station_` is (not) mutated
    // below.
    bool maneuver = false;
    MotionResult motionResult; // Default: invalid, nothing technological happened.
    if (ride && known_) {
      const int to = shaftRide ? lifted : targetFor(ph, d);
      maneuver = to == from;
      if (!maneuver) {
        if (pendDir_) flushLiftCycle("no_return"); // Left an RPS_seat station before the way back.
        station_ = to;
      } else if (const StationRow *row = stationRow(station_);
                 row && (row->motion == Motion::ZTransfer || row->motion == Motion::Compound)) {
        // Same station before and after: not a transport ride. `flat()`
        // split `p` into the horizontal component `ph` (its norm is `d`)
        // and whatever lies along gravity, which `flat()` removed — that
        // removed component is the vertical (lift) distance, not `ph.z`
        // (near-zero by construction: `ph` is perpendicular to gravity).
        // RotationZ is logged from Tracker::endTurn() instead: a turn at a
        // parked station almost never satisfies `moved`, and `turnSoFar()`
        // is already 0 by the time finish() could run anyway.
        const float verticalM = dot(p, rest_.g) / std::max(norm(rest_.g), 1e-6f);
        motionResult = classifyMotion(*row, std::fabs(verticalM), 0);
      }
    }
    if (ride && !carried && !maneuver) {
      formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000),
                         "transport", knownBefore ? from : -1, known_ ? station_ : -1,
                         "", Motion::None, shaftRide ? "Z" : "", d, 0, slow_ ? 1.0f : 0.5f, refFix_,
                         knownBefore && known_ ? declaredSpan(from, station_) : 0);
      float rms[3];
      for (int i = 0; i < 3; ++i) rms[i] = n_ ? float(std::sqrt(sq_[i] / n_)) : 0;
      appendRideColumns(distanceRow_, sizeof(distanceRow_), T, ramp_, rms, peak_);
      setLogName(false, knownBefore ? from : -1, known_ ? station_ : -1);
      pushRow();
    } else if (motionResult.valid) {
      const StationRow *row = stationRow(station_);
      formatDistanceRow(distanceRow_, sizeof(distanceRow_), uint32_t(clock_ * 1000),
                         "technological", -1, -1, row ? row->id : "", motionResult.motion,
                         "Z", motionResult.measuredDistanceM, motionResult.measuredAngleDeg,
                         1.0f, motionResult.overRange ? "over_range" : "",
                         row ? std::fabs(row->motionRange) : 0);
      setLogName(true, station_, station_);
      pushRow();
    }
    if ((ride && !maneuver) || carried) {
      Transit &t = transits_[transitHead_];
      t = Transit{};
      t.id = lastTransitId_ = nextId_++;
      t.from = knownBefore ? from : -1;
      t.to = known_ ? station_ : -1;
      t.carried = carried;
      t.lift = shaftRide ? (vz > 0 ? 1 : -1) : 0;
      t.distance = d;
      t.durationS = T;
      t.floorDeg = floorDir * 180 / pi;
      t.ramp = ramp_;
      for (int i = 0; i < 3; ++i) {
        t.rms[i] = n_ ? float(std::sqrt(sq_[i] / n_)) : 0;
        t.peak[i] = peak_[i];
      }
      if (known_) for (int i = 0; i < 3; ++i) stationVibMax_[station_][i] = std::max(stationVibMax_[station_][i], t.rms[i]);
      // Up to the stop only, merged down to `envBins`.
      int used = int(T / (workBinS * perBin_)) + 1;
      used = used > work_ ? work_ : used;
      const int merge = (used + envBins - 1) / envBins;
      t.bins = (used + merge - 1) / merge;
      t.binS = workBinS * perBin_ * merge;
      for (int b = 0; b < t.bins; ++b)
        for (int i = 0; i < 3; ++i) {
          int16_t lo = 32767, hi = -32768;
          for (int k = b * merge; k < (b + 1) * merge && k < used; ++k) {
            lo = std::min(lo, workLo_[k][i]);
            hi = std::max(hi, workHi_[k][i]);
          }
          t.lo[b][i] = lo;
          t.hi[b][i] = hi;
        }
      transitHead_ = (transitHead_ + 1) % maxTransits;
      if (transitCount_ < maxTransits) ++transitCount_;
      newTransit_ = true;
    }
    refFix_[0] = 0;
    lastDistance_ = d;
    if (known_ && headingKnown_) {
      learned.heading[station_] = heading_;
      learned.headingKnown[station_] = true;
    }
    // Standing again: gravity in the sensor axes now, the gyro offset is the
    // sensor's own and stays.
    gB_ = gMid_ = rotate(conj(q_), g1);
    bias_ = biasMid_ = rest_.bias;
    for (Rest &h : hist_) h = {gB_, bias_};
    wLp_ = {};
    yawLp_ = 0;
    clearDetector(); // Its filters were in the ride's axes.
    phase_ = Phase::Parked;
    phaseStart_ = quietStart_; // Parked since the stop.
    parkDone_ = true;
    wantScan();
    arrived_ = false;
  }

public:
  float lastDistance() const { return lastDistance_; }
};
} // namespace line
