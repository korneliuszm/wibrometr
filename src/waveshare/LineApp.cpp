#include "LineApp.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstdlib>
#include "Qmi8658Decode.h"
#include "UiTheme.h"
#include "UsbStorage.h"
#include "WebLink.h"
#include "WifiScan.h"

namespace lineapp {

namespace {
// Big objects live in PSRAM: created on first use, after PSRAM is up.
DisplacementMeter *meter = nullptr;
line::Tracker *track = nullptr;
Preferences prefs;
hal::SdLogger *sd = nullptr;
bool configGood = false;
line::ConfigError configErr{0, ""};
Decimator decimator;
Gyro gyroState;
Accel latestGyro{};
uint32_t gyroUs = 0, movesLogged = 0;
bool gyroPrimed = false;
const char *pendingNotice = nullptr;

constexpr float toDeg = 1 / qmi8658::kRadPerDeg;

void notice(const char *text) { pendingNotice = text; }

void saveLearned() { prefs.putBytes("learned", &track->learned, sizeof track->learned); }
void loadLearned() {
  // ~5 KB: read straight into the tracker, undone if it is not a valid copy.
  line::Learned &l = track->learned;
  if (prefs.getBytesLength("learned") != sizeof l || prefs.getBytes("learned", &l, sizeof l) != sizeof l ||
      l.version != line::Learned::currentVersion)
    track->forgetAll();
}

// Two buffers in turn: one printf may name two tables.
const char *tableName(int s) {
  static char text[2][8];
  static int next = 0;
  if (s < 0) return "?";
  char *out = text[next++ & 1];
  snprintf(out, 8, "%d", s + 1);
  return out;
}

// Rides and technological motions: FFat (also the USB drive) and the TF card.
void writeLogRows() {
  const char *file = nullptr, *row = nullptr;
  const int64_t epoch = net::epochMs();
  const uint32_t clock = track->clockMs();
  while (track->takeDistanceRow(file, row)) {
    char *end = nullptr;
    const unsigned long event = strtoul(row, &end, 10);
    char time[32], csv[288];
    if (end == row || *end != ',' || event > UINT32_MAX ||
        !line::formatEventTime(time, sizeof time, epoch, clock, uint32_t(event)) ||
        !line::prependLogTime(csv, sizeof csv, row, time)) {
      Serial.println("logs: nieprawidlowy wiersz lub brak miejsca w buforze");
      continue;
    }
    storage::appendLogRow(file, csv);
    if (sd) sd->appendRideRow(file, csv);
    Serial.printf("log logs/%s.csv: %s", file, csv);
  }
}

// Wi-Fi scans the tracker asks for, finished transits, saving what was learned.
void serviceLine() {
  line::Tracker &t = *track;
  if (t.scanWanted() && !wifiscan::busy()) {
    t.scanStarted();
    wifiscan::start();
    Serial.println("line: parked, Wi-Fi fingerprint scan");
  }
  wifiscan::poll();
  line::Fingerprint scan;
  if (wifiscan::take(scan)) {
    t.applyScan(scan);
    Serial.printf("line: %d access points, table %s%s%s\n", scan.count, t.known() ? tableName(t.station()) : "?",
                  t.lastCorrected() ? " (set by Wi-Fi)" : "", t.known() ? "" : ", unknown");
    if (t.lastCorrected()) notice("STOL USTALONY PRZEZ WI-FI");
    weblink::publishLine(t);
  }
  if (t.newTransit()) {
    const line::Transit &x = t.transit(t.transitCount() - 1);
    Serial.printf("transit %lu: table %s -> %s, %.2f m at %+.0f deg, %.1f s, ramp %.2f m/s2, vib rms %.2f %.2f %.2f m/s2\n",
                  (unsigned long)x.id, tableName(x.from), tableName(x.to), x.distance, x.floorDeg, x.durationS, x.ramp,
                  x.rms[0], x.rms[1], x.rms[2]);
    weblink::publishLine(t);
  }
  writeLogRows();
  if (t.newRefFix()) {
    static char text[40];
    snprintf(text, sizeof text, "KOREKTA: PUNKT ODNIES. %s", t.refFixStation());
    Serial.printf("line: reference point, station set to %s\n", t.refFixStation());
    notice(text);
    weblink::publishLine(t);
  }
  if (t.newTurn()) {
    Serial.printf("line: turned %+.0f deg (turntable), heading %+.0f deg (about Z as shown: right +)\n",
                  ui::shown(t.lastTurn(), 2) * 57.29578f, ui::shown(t.heading(), 2) * 57.29578f);
    notice(t.lastTurn() > 0 ? "OBROT W LEWO" : "OBROT W PRAWO");
  }
  if (t.learnedChanged()) saveLearned();
}

void handleWebCommand() {
  switch (weblink::takeCommand()) {
  case weblink::Command::ResetTotals:
    resetTotals();
    notice("SUMA WYZEROWANA ZE STRONY");
    break;
  case weblink::Command::Home:
    if (!configGood) break;
    home();
    notice("STOL 1 USTAWIONY ZE STRONY");
    break;
  case weblink::Command::ForgetWifi:
    track->forgetWifi();
    saveLearned();
    weblink::publishLine(*track);
    break;
  case weblink::Command::None: break;
  }
}

} // namespace

line::Tracker &tracker() { return *track; }
DisplacementMeter &displacement() { return *meter; }
bool configOk() { return configGood; }
const line::ConfigError &configError() { return configErr; }
const Gyro &gyro() { return gyroState; }

void home() {
  if (!configGood) return;
  track->home();
  weblink::publishLine(*track);
}
void resetTotals() {
  meter->resetTotal();
  movesLogged = 0;
}
void zeroGyroAngle() { gyroState.angle = {}; }
const char *takeNotice() {
  const char *n = pendingNotice;
  pendingNotice = nullptr;
  return n;
}

void begin(hal::SdLogger *logger) {
  sd = logger;
  meter = new DisplacementMeter();
  track = new line::Tracker();
  prefs.begin("line");
  loadLearned();
  // config.csv is applied AFTER the NVS load so it is always the last write:
  // the CSV wins over learned/auto state.
  const bool storageReady = storage::begin();
  if (storageReady) {
    static line::StationConfig cfg; // ~2 KB with 64 rows: not on the setup stack.
    configGood = storage::readConfig(cfg, configErr);
    if (!configGood) weblink::setConfigProblem(configErr.line, configErr.what);
    else {
      track->applyStationConfig(cfg);
      Serial.printf("config.csv: loaded, %d stations\n", cfg.count);
    }
    if (!configGood)
      Serial.printf("config.csv: NOT VALID (line %d: %s) - the line is not tracked\n", configErr.line, configErr.what);
  } else {
    configErr = {0, "pamiec dysku niedostepna"};
    weblink::setConfigProblem(configErr.line, configErr.what);
    Serial.println("USB storage: FFat.begin failed - the line is not tracked");
  }
  net::begin(storageReady);
  weblink::begin();
  storage::startUsb(); // Configuration reads finish before host sector access.
}

void onGap() {
  meter->breakSignal();
  track->breakSignal();
  decimator.reset();
  gyroPrimed = false;
}

void onSample(const ImuEvent &e) {
  latestGyro = e.gyro;
  // Angle per axis: bias-free rate integrated (exact for a turn about one axis).
  if (gyroPrimed && uint32_t(e.us - gyroUs) < 50000 && meter->biasKnown()) {
    const float dt = (e.us - gyroUs) * 1e-6f * toDeg;
    const Accel b = meter->gyroBias();
    gyroState.angle = {gyroState.angle.x + (e.gyro.x - b.x) * dt, gyroState.angle.y + (e.gyro.y - b.y) * dt,
                       gyroState.angle.z + (e.gyro.z - b.z) * dt};
  }
  gyroUs = e.us;
  gyroPrimed = true;
  const Accel b = meter->gyroBias();
  const float k = 0.1f;
  gyroState.shown = {gyroState.shown.x + k * ((e.gyro.x - b.x) * toDeg - gyroState.shown.x),
                     gyroState.shown.y + k * ((e.gyro.y - b.y) * toDeg - gyroState.shown.y),
                     gyroState.shown.z + k * ((e.gyro.z - b.z) * toDeg - gyroState.shown.z)};

  Decimator::Out s;
  if (!decimator.add(e.value, e.gyro, e.us, s)) return;
  track->add(s.a, s.w, s.us);
  meter->add(s.a, s.w, s.us);
  const Accel bias = meter->gyroBias();
  weblink::addSample(s.a, {(s.w.x - bias.x) * toDeg, (s.w.y - bias.y) * toDeg, (s.w.z - bias.z) * toDeg}, s.us);
  if (meter->moves() != movesLogged) {
    movesLogged = meter->moves();
    weblink::publishMove(*meter);
    const auto &mv = meter->last();
    Serial.printf("move %lu: dx=%+.1f dy=%+.1f dz=%+.1f cm, %.2f s, vmax %.2f m/s, turn %.1f deg%s\n",
                  (unsigned long)movesLogged, 100 * mv.d[0], 100 * mv.d[1], 100 * mv.d[2], mv.durationS, mv.peakSpeed,
                  mv.rotationDeg, mv.tilted ? ", TILTED" : "");
  }
}

void service(uint32_t now, bool live, float rateHz) {
  net::loop(now, wifiscan::busy());
  serviceLine();
  weblink::loop(now, *meter, *track, rateHz, live, wifiscan::busy());
  handleWebCommand();
}

} // namespace lineapp
