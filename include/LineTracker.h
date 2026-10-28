#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "SignalHistory.h"

// Sensor output rate the tracker is built for (the firmware sets it).
#ifndef LINE_SAMPLE_HZ
#define LINE_SAMPLE_HZ 200
#endif

// Position of a car body riding a roller line between numbered tables
// (stations). The line may run in X and in Y, tables may lift the body (that
// motion is ignored) and turntables may turn it by 90 degrees either way.
//
// Attitude: the gyro (minus its offset) turns the expected gravity in the
// sensor axes all the time, standing or riding; the accelerometer only pulls
// it back slowly. So tilting (lifting tables) or turning (turntables) does not
// look like acceleration. The heading (turn about the vertical) is integrated
// the same way; a turn while standing is reported, and snapped to a multiple
// of 90 degrees when close to one.
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
// Tables: each gap between tables learns its floor direction from the first
// ride through it. A ride is forward or back by comparing with those
// directions, and moves the position by distance / spacing tables. Rides
// without braking (a tilt or a jolt) move nothing.
//
// Wi-Fi: a stop longer than `parkS` confirms the table and asks for a Wi-Fi
// fingerprint. The first one at a table becomes its reference; later ones are
// compared with all references and can correct the table counted from motion,
// or find the table from scratch after a power cycle.
//
// Each ride also records its vibration: RMS and peak of the high-passed
// acceleration per axis, the largest ramp (smoothed) acceleration and an
// envelope of the acceleration without gravity over time.
namespace line {
constexpr int maxStations = 16;
constexpr int defaultStations = 8;
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
  int bins = 0;
  float binS = 0;                // Time per envelope point.
  int16_t lo[envBins][3] = {}, hi[envBins][3] = {}; // Acceleration without gravity, 0.01 m/s2.
};

// What is learned and kept across power cycles.
struct Learned {
  static constexpr uint32_t currentVersion = 2;
  uint32_t version = currentVersion;
  int stations = defaultStations;
  float gap[maxStations] = {};       // Metres from table i to i+1; 0: not learned.
  float gapDir[maxStations] = {};    // Floor direction from table i to i+1, rad.
  bool gapDirKnown[maxStations] = {};
  float heading[maxStations] = {};   // Heading of the body when it last stood at table i, rad.
  bool headingKnown[maxStations] = {};
  Fingerprint fp[maxStations];
};

} // namespace line
#include "StationConfig.h"
namespace line {

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
  static constexpr float leakTauS = 1.0f;      // Leak of the velocity used to spot a ride.
  static constexpr float startK = 4.0f, minStartV = 0.05f, maxStartV = 0.25f;  // m/s
  static constexpr float stillK = 3.5f, minStillA = 0.05f, maxStillA = 0.4f;   // m/s2
  static constexpr float startConfirmS = 0.15f;
  static constexpr float forcedStopS = 20.0f;  // Calm this long ends a ride without braking.
  static constexpr float stopS = 2.0f;         // Calm this long: shown as arrived.
  static constexpr float parkS = 15.0f;        // Calm this long: table confirmed, Wi-Fi scan.
  static constexpr float turnStart = 0.035f;   // rad/s (2 deg/s) about the vertical: turning.
  static constexpr float turnEnd = 0.009f;     // rad/s (0.5 deg/s) ...
  static constexpr float turnEndS = 1.0f;      // ... this long: the turn is over.
  static constexpr float minTurn = 0.35f;      // rad (20 deg): smaller is not a turn.
  static constexpr float snapTol = 0.35f;      // rad: within this of k * 90 deg, snap to it.
  static constexpr float minTransitM = 1.5f;   // Shorter: a nudge, same table.
  static constexpr float defaultGapM = 6.5f;
  static constexpr float nudgeShare = 0.35f;   // Under this share of a spacing: same table.
  static constexpr float maxTransitS = 60.0f;  // Longer: not a ride, table unknown.
  static constexpr float abandonS = 120.0f;    // "Riding" this long: the attitude is lost; start over.
  // Carried elsewhere instead of riding the rollers: a roller ride stays
  // level, does not turn and ends with braking.
  static constexpr float carryTilt = 0.14f;    // rad (8 deg) of tilt during the move.
  static constexpr float carryYaw = 0.52f;     // rad (30 deg) of turn before the stop.
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

  void setStations(int n) {
    learned.stations = n < 2 ? 2 : n > maxStations ? maxStations : n;
    if (station_ >= learned.stations) station_ = learned.stations - 1;
  }
  // Loads a parsed config.csv as the authoritative station layout: gaps and
  // floor directions come from `cfg` and are no longer auto-learned, and
  // Wi-Fi scans stop correcting the counted station (still scanned and
  // blended into fingerprints, for the dashboard and distance log).
  // Call once, at boot, before `home()`.
  void applyStationConfig(const StationConfig &cfg) {
    setStations(cfg.count);
    for (int i = 0; i < cfg.count; ++i) {
      stationRow_[i] = cfg.row[i];
      if (i + 1 < cfg.count) {
        learned.gap[i] = cfg.row[i + 1].distanceM;
        const float dir = directionAngle(cfg.row[i + 1].direction);
        learned.gapDir[i] = dir;
        learned.gapDirKnown[i] = true;
      }
    }
    configAuthoritative_ = true;
  }
  bool configAuthoritative() const { return configAuthoritative_; }
  // Configured row for station `s`, or nullptr if no config.csv was loaded
  // or `s` is out of range. Used by the motion classifier and distance log
  // in later tasks.
  const StationRow *stationRow(int s) const {
    return configAuthoritative_ && s >= 0 && s < learned.stations ? &stationRow_[s] : nullptr;
  }
  // The body is at table 1 now; its heading there is the floor's 0.
  void home() {
    station_ = 0;
    known_ = true;
    lost_ = Lost::None;
    heading_ = 0;
    headingKnown_ = true;
    if (phase_ == Phase::Moving) phase_ = Phase::Settling;
    if (phase_ == Phase::Parked) scanWanted_ = true;
  }
  void forgetWifi() { for (auto &f : learned.fp) f.count = 0; lastScan_.count = 0; }
  // In place: a fresh Learned would be ~5 KB on the stack.
  void forgetAll() {
    learned.version = Learned::currentVersion;
    if (learned.stations < 2 || learned.stations > maxStations) learned.stations = defaultStations;
    for (int i = 0; i < maxStations; ++i) {
      learned.gap[i] = learned.gapDir[i] = learned.heading[i] = 0;
      learned.gapDirKnown[i] = learned.headingKnown[i] = false;
    }
    forgetWifi();
  }

  void add(Accel a, Accel w, uint32_t us) {
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
      case Phase::Parked:
        follow(dt, levelTauS);
        if (hv_ > startThreshold()) {
          if ((overS_ += dt) >= startConfirmS) startMove();
        } else overS_ = 0;
        if (phase_ == Phase::Parked && !parkDone_ && clock_ - phaseStart_ >= parkS) { parkDone_ = true; scanWanted_ = true; }
        // Table still unknown after a scan: try again now and then.
        if (phase_ == Phase::Parked && !known_ && relocateAt_ >= 0 && clock_ >= relocateAt_) {
          relocateAt_ = -1;
          scanWanted_ = true;
        }
        break;
      case Phase::Moving: {
        // Safety net: no ride or carry lasts this long. Whatever happened,
        // the attitude cannot be trusted: learn gravity again, let Wi-Fi
        // find the table.
        if (t_ > abandonS) { abandon(); break; }
        // Braking: the leaky velocity points against the way the ride
        // started. Speeding up again after it cancels it.
        const float along = dot(vlFlat_, rideDir_), limit = startThreshold();
        if (along < -limit) braked_ = true;
        else if (along > limit) braked_ = false;
        // Calm: no ride-like velocity and little low-frequency acceleration.
        // A steady roll is calm too, so a stop also needs the braking before
        // it (or a calm longer than any steady roll).
        const bool calm = ha_ < stillThreshold() && hv_ < limit;
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
        const bool stopped = calmS >= (slow_ ? stopS : forcedStopS);
        if (stopped && !arrived_) {
          arrived_ = true;
          arrivalStation_ = slow_ && known_ ? targetFor(flat(snap_.p), snapLive_) : station_;
        }
        if (stopped && calmS >= parkS) finish();
        break;
      }
    }
  }

  // Wi-Fi: call when a scan wanted by `scanWanted()` has started / finished.
  bool scanWanted() const { return scanWanted_; }
  void scanStarted() { scanWanted_ = false; }
  void applyScan(const Fingerprint &scan) {
    lastScan_ = scan;
    const int n = learned.stations;
    for (int s = 0; s < maxStations; ++s) score_[s] = s < n && learned.fp[s].count ? distance(scan, learned.fp[s]) : -1;
    if (scan.count == 0) return;
    // Best table: Wi-Fi likeness plus a cost for disagreeing with the count.
    int best = -1;
    float bestCost = 1e9f;
    for (int s = 0; s < n; ++s) {
      if (score_[s] < 0) continue;
      const float cost = score_[s] + (known_ ? stepCostDb * std::abs(s - station_) : 0);
      if (cost < bestCost) { bestCost = cost; best = s; }
    }
    corrected_ = false;
    if (!known_) {
      if (configAuthoritative_) return; // CSV is authoritative: never locate purely from Wi-Fi.
      // Table from Wi-Fi alone (after boot, lost samples or being carried):
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
    // Overrule the count only against a table that has its own reference
    // (while learning, the current table has none yet).
    else if (!configAuthoritative_ && known_ && best >= 0 && best != station_ && score_[station_] >= 0 &&
             bestCost + switchMarginDb < score_[station_]) {
      station_ = best;
      corrected_ = true;
      if (transitCount_ && transits_[(transitHead_ + maxTransits - 1) % maxTransits].id == lastTransitId_) {
        Transit &t = transits_[(transitHead_ + maxTransits - 1) % maxTransits];
        t.to = station_;
        t.wifiCorrected = true;
      }
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
  float liveDistance() const { return phase_ == Phase::Moving ? horizontal(arrived_ ? snap_.p : p_) : 0; }
  // Position as a fractional table index, for the animation.
  float position() const {
    if (phase_ != Phase::Moving) return float(station_);
    if (arrived_) return float(arrivalStation_);
    const float d = liveDistance();
    const int dir = direction(flat(p_));
    float pos = station_ + dir * (d / gapAhead(dir));
    const float maxPos = float(learned.stations - 1);
    return pos < 0 ? 0 : pos > maxPos ? maxPos : pos;
  }
  int direction() const { return phase_ == Phase::Moving ? direction(flat(p_)) : 0; }
  float gap(int i) const { return learned.gap[i] > 0 ? learned.gap[i] : 0; }
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

  // Transits, oldest first; `newTransit()` is true once per finished transit.
  int transitCount() const { return transitCount_; }
  const Transit &transit(int i) const { return transits_[(transitHead_ + maxTransits - transitCount_ + i) % maxTransits]; }
  bool newTransit() { const bool r = newTransit_; newTransit_ = false; return r; }
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
  static constexpr float stepCostDb = 4.0f;     // Per table away from the counted one.
  static constexpr float switchMarginDb = 3.0f; // Needed to overrule the count.
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
  float t_ = 0;
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
  bool configAuthoritative_ = false;
  StationRow stationRow_[maxStations];
  static float directionAngle(Direction d) {
    switch (d) {
      case Direction::XPlus: return 0;
      case Direction::XMinus: return pi;
      case Direction::YPlus: return pi / 2;
      case Direction::YMinus: return -pi / 2;
    }
    return 0;
  }
  Transit transits_[maxTransits];
  int transitHead_ = 0, transitCount_ = 0;
  float lastDistance_ = 0;
  Lost lost_ = Lost::Boot;
  float relocateAt_ = -1;   // When to scan again while the table is unknown; <0: not planned.
  float minUpCos_ = 1;      // Most tilt during the move (cos of the angle), up to the stop.
  float scoreSum_[maxStations] = {}; // Wi-Fi scores summed over scans at one spot while lost.
  int scoreN_ = 0;
  void clearScores() { for (float &v : scoreSum_) v = 0; scoreN_ = 0; }

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
    if (std::fabs(turned - snapped) < snapTol) heading_ = turnFrom_ + snapped;
    lastTurn_ = heading_ - turnFrom_;
    turnAt_ = clock_;
    ++turnCount_;
    newTurn_ = true;
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
    hist_[(head_ + restSamples - 1) % restSamples] = {gB_, bias_}; // Slot of the sample just added.
  }
  void clearDetector() { lp1_ = lp2_ = vl_ = vlFlat_ = vlMean_ = haMean_ = {}; ha_ = hv_ = overS_ = 0; }
  void abandon() {
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
  // Forward (+1) or back (-1) along the line, from the learned directions of
  // the gaps next to the current table; forward when nothing is known.
  int direction(Accel ph) const {
    if (!headingKnown_ || norm(ph) < 0.05f) return 1;
    const float ang = floorAngle(ph);
    const int s = station_;
    const bool f = s < learned.stations - 1 && learned.gapDirKnown[s];
    const bool b = s > 0 && learned.gapDirKnown[s - 1];
    const float cf = f ? std::cos(ang - learned.gapDir[s]) : 0;
    const float cb = b ? std::cos(ang - learned.gapDir[s - 1] - pi) : 0;
    if (f && b) return cf >= cb ? 1 : -1;
    if (f) return cf > -0.5f ? 1 : -1;
    if (b) return cb > 0.5f ? -1 : 1;
    return s == learned.stations - 1 ? -1 : 1;
  }
  float gapAhead(int dir) const {
    const int i = dir > 0 ? station_ : station_ - 1;
    return i >= 0 && i < maxStations && learned.gap[i] > 0 ? learned.gap[i] : defaultGapM;
  }
  // Table reached after `d` metres: walk gap by gap with the learned spacings
  // (rounded to the nearest table); a short nudge stays at the same table.
  int targetFor(Accel ph, float d) const {
    const int dir = direction(ph);
    if (d < minTransitM || d < nudgeShare * gapAhead(dir)) return station_;
    int s = station_;
    float left = d;
    for (;;) {
      const int i = dir > 0 ? s : s - 1;
      if (i < 0 || i >= learned.stations - 1) break;
      const float g = learned.gap[i] > 0 ? learned.gap[i] : defaultGapM;
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
    rest_ = hist_[head_];
    // Heading at the start of the pre-roll: take back what the pre-roll turned.
    rideHeading_ = heading_;
    // The way the ride started (leaky velocity), to recognise its braking.
    const float n = norm(vlFlat_);
    rideDir_ = n > 1e-6f ? scale(vlFlat_, 1 / n) : Accel{};
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
    t_ = 0;
    first_ = true;
    for (double &s : sq_) s = 0;
    for (float &p : peak_) p = 0;
    ramp_ = 0;
    n_ = 0;
    work_ = 0;
    perBin_ = 1;
    binOpen_ = false;
    for (int i = 0; i < restSamples; ++i) {
      const Sample &s = ring_[(head_ + i) % restSamples];
      step(s.a, s.w, i ? s.dt : 0);
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
    const float d = norm(ph);
    const float floorDir = floorAngle(ph);
    const int from = station_;
    const bool knownBefore = known_;
    // Carried elsewhere rather than ridden: tilted, turned on the way, far
    // longer than a ride, or a long move without braking. Distance and
    // direction mean nothing then; the table comes from Wi-Fi alone.
    const Accel up = scale(rest_.g, 1 / std::max(norm(rest_.g), 1e-6f));
    const float yawToStop = 2 * std::atan2(dot({snap_.q.x, snap_.q.y, snap_.q.z}, up), snap_.q.w);
    const bool moved = d >= minTransitM;
    const bool carried = moved && (minUpCos_ < std::cos(carryTilt) || std::fabs(wrap(yawToStop)) > carryYaw ||
                                   T > maxTransitS || !slow_);
    if (carried && known_) { known_ = false; lost_ = Lost::Carried; }
    const bool ride = moved && slow_ && !carried;
    if (ride && known_) {
      const int to = targetFor(ph, d);
      if (std::abs(to - from) == 1 && !configAuthoritative_) { // Learn this gap: spacing and floor direction.
        const int i = to > from ? from : to;
        float &g = learned.gap[i];
        g = g > 0 ? g + 0.5f * (d - g) : d;
        if (headingKnown_) {
          const float dir = to > from ? floorDir : wrap(floorDir + pi);
          if (!learned.gapDirKnown[i]) { learned.gapDir[i] = dir; learned.gapDirKnown[i] = true; }
          else learned.gapDir[i] = wrap(learned.gapDir[i] + 0.3f * wrap(dir - learned.gapDir[i]));
        }
        learnedChanged_ = true;
      }
      station_ = to;
    }
    if (ride || carried) {
      Transit &t = transits_[transitHead_];
      t = Transit{};
      t.id = lastTransitId_ = nextId_++;
      t.from = knownBefore ? from : -1;
      t.to = known_ ? station_ : -1;
      t.carried = carried;
      t.distance = d;
      t.durationS = T;
      t.floorDeg = floorDir * 180 / pi;
      t.ramp = ramp_;
      for (int i = 0; i < 3; ++i) {
        t.rms[i] = n_ ? float(std::sqrt(sq_[i] / n_)) : 0;
        t.peak[i] = peak_[i];
      }
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
    scanWanted_ = true;
    arrived_ = false;
  }

public:
  float lastDistance() const { return lastDistance_; }
};
} // namespace line
