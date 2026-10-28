#include "WebLink.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "WebPage.h"
#include "WifiScan.h"

namespace weblink {
namespace {
constexpr uint32_t batchMs = 50;
constexpr int maxRows = 64; // 0.3 s at 200 Hz; older rows are dropped if sending stalls.
struct Row { Accel a, g; uint32_t us; };

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
std::atomic<bool> greetWanted{false};
std::atomic<uint8_t> command{uint8_t(Command::None)};
std::atomic<int> commandArg{0};
Row rows[maxRows];
int rowCount = 0;
uint32_t lastSend = 0, prevUs = 0;
bool havePrev = false;
// Text buffers (~25 KB) are allocated in begin(), so they land in PSRAM.
constexpr int batchSize = maxRows * 64 + 512, moveSize = DisplacementMeter::trackPoints * 24 + 256,
              lineSize = 8192, transitSize = line::envBins * 40 + 512;
char *batch = nullptr, *moveText = nullptr, *lineText = nullptr, *transitText = nullptr;
char ip[16] = "192.168.4.1";

const char *stateName(DisplacementMeter::State s) {
  switch (s) {
    case DisplacementMeter::State::Settling: return "settling";
    case DisplacementMeter::State::Rest: return "rest";
    case DisplacementMeter::State::Moving: return "moving";
    case DisplacementMeter::State::TooLong: return "toolong";
  }
  return "settling";
}
const char *phaseName(line::Tracker::Phase p) {
  switch (p) {
    case line::Tracker::Phase::Settling: return "settle";
    case line::Tracker::Phase::Parked: return "park";
    case line::Tracker::Phase::Moving: return "move";
  }
  return "settle";
}

// Runs on the server task: only flags, handled by the loop task.
void onEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    greetWanted = true; // A new page gets the line, transits and last move straight away.
  } else if (type == WS_EVT_DATA) {
    const auto *info = static_cast<AwsFrameInfo *>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > 31) return;
    char text[32];
    std::memcpy(text, data, len);
    text[len] = 0;
    Command c = Command::None;
    int a = 0;
    if (!std::strcmp(text, "reset")) c = Command::ResetTotals;
    else if (!std::strcmp(text, "home")) c = Command::Home;
    else if (!std::strcmp(text, "forgetwifi")) c = Command::ForgetWifi;
    else if (!std::strcmp(text, "forgetall")) c = Command::ForgetAll;
    else if (!std::strncmp(text, "stations:", 9)) { c = Command::Stations; a = std::atoi(text + 9); }
    if (c == Command::None) return;
    commandArg = a;
    command = uint8_t(c);
  }
}

int appendFingerprint(char *out, int room, const line::Fingerprint &f) {
  int n = snprintf(out, room, "[");
  for (int i = 0; i < f.count && n < room - 40; ++i) {
    const uint8_t *b = f.ap[i].bssid;
    n += snprintf(out + n, room - n, "%s[\"%02x%02x%02x%02x%02x%02x\",%.1f]", i ? "," : "", b[0], b[1], b[2], b[3],
                  b[4], b[5], f.ap[i].rssi);
  }
  return n + snprintf(out + n, room - n, "]");
}
} // namespace

void begin() {
  batch = static_cast<char *>(malloc(batchSize));
  moveText = static_cast<char *>(malloc(moveSize));
  lineText = static_cast<char *>(malloc(lineSize));
  transitText = static_cast<char *>(malloc(transitSize));
  if (!batch || !moveText || !lineText || !transitText) {
    Serial.println("web: no memory for buffers, page disabled");
    return;
  }
  WiFi.mode(WIFI_AP_STA); // STA side only scans for the line's Wi-Fi fingerprints.
  WiFi.softAP(ssid, password, 6, 0, 4);
  snprintf(ip, sizeof ip, "%s", WiFi.softAPIP().toString().c_str());
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", reinterpret_cast<const uint8_t *>(webPage), strlen(webPage));
  });
  server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });
  server.begin();
  Serial.printf("Wi-Fi AP \"%s\" (password \"%s\"), page http://%s/\n", ssid, password, ip);
}

void addSample(const Accel &accel, const Accel &gyroDps, uint32_t us) {
  if (ws.count() == 0) { rowCount = 0; havePrev = false; return; }
  if (rowCount == maxRows) { // Sending stalled (e.g. during a Wi-Fi scan): keep the newest.
    std::memmove(rows, rows + 1, sizeof(Row) * (maxRows - 1));
    --rowCount;
  }
  rows[rowCount++] = {accel, gyroDps, us};
}

void loop(uint32_t now, const DisplacementMeter &meter, const line::Tracker &tracker, float rateHz, bool live,
          bool scanning) {
  if (greetWanted.exchange(false)) {
    publishLine(tracker);
    for (int i = 0; i < tracker.transitCount(); ++i) publishTransit(tracker.transit(i));
    if (meter.moves()) publishMove(meter);
  }
  if (!batch || uint32_t(now - lastSend) < batchMs) return;
  lastSend = now;
  ws.cleanupClients();
  if (ws.count() == 0 || !ws.availableForWriteAll()) return;
  int n = snprintf(batch, batchSize,
                   "{\"t\":\"s\",\"hz\":%.1f,\"live\":%d,\"st\":\"%s\",\"ms\":%.2f,\"n\":%lu,"
                   "\"lv\":[%.2f,%.2f,%.2f],\"tot\":[%.2f,%.2f,%.2f],"
                   "\"ln\":{\"n\":%d,\"cur\":%d,\"k\":%d,\"ph\":\"%s\",\"arr\":%d,\"pos\":%.3f,\"dir\":%d,"
                   "\"t\":%.1f,\"wait\":%.1f,\"d\":%.2f,\"scan\":%d,"
                   "\"act\":%.3f,\"thr\":%.3f,\"shk\":%.3f,\"sthr\":%.3f,"
                   "\"hd\":%.1f,\"hk\":%d,\"turn\":%d,\"tn\":%.1f,\"lt\":%.1f,\"ltS\":%.1f,"
                   "\"lost\":%d,\"carry\":%d},\"r\":[",
                   rateHz, live ? 1 : 0, stateName(meter.state()), meter.movingS(), (unsigned long)meter.moves(),
                   100 * meter.live(0), 100 * meter.live(1), 100 * meter.live(2), 100 * meter.total(0),
                   100 * meter.total(1), 100 * meter.total(2), tracker.learned.stations, tracker.station(),
                   tracker.known() ? 1 : 0, phaseName(tracker.phase()), tracker.arrived() ? 1 : 0, tracker.position(),
                   tracker.direction(), tracker.phaseS(), tracker.untilPark(), tracker.liveDistance(),
                   scanning ? 1 : 0, tracker.activity(), tracker.startThreshold(), tracker.shaking(),
                   tracker.stillThreshold(), tracker.heading() * 57.29578f, tracker.headingKnown() ? 1 : 0,
                   tracker.turning() ? 1 : 0, tracker.turnSoFar() * 57.29578f, tracker.lastTurn() * 57.29578f,
                   tracker.sinceTurnS(), int(tracker.lost()), tracker.carrying() ? 1 : 0);
  // Rows: [microseconds since the previous sample, ax, ay, az, gx, gy, gz].
  for (int i = 0; i < rowCount && n < int(batchSize) - 80; ++i) {
    const Row &r = rows[i];
    const uint32_t d = havePrev ? r.us - prevUs : 0;
    prevUs = r.us;
    havePrev = true;
    n += snprintf(batch + n, batchSize - n, "%s[%lu,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f]", i ? "," : "",
                  (unsigned long)d, r.a.x, r.a.y, r.a.z, r.g.x, r.g.y, r.g.z);
  }
  n += snprintf(batch + n, batchSize - n, "]}");
  rowCount = 0;
  ws.textAll(batch, n);
}

void publishMove(const DisplacementMeter &meter) {
  if (!moveText || ws.count() == 0) return;
  const auto &m = meter.last();
  int n = snprintf(moveText, moveSize,
                   "{\"t\":\"m\",\"n\":%lu,\"d\":[%.2f,%.2f,%.2f],\"dur\":%.2f,\"v\":%.3f,\"rot\":%.1f,"
                   "\"tilt\":%d,\"tr\":[",
                   (unsigned long)meter.moves(), 100 * m.d[0], 100 * m.d[1], 100 * m.d[2], m.durationS,
                   m.peakSpeed, m.rotationDeg, m.tilted ? 1 : 0);
  for (int k = 0; k < m.points && n < int(moveSize) - 40; ++k)
    n += snprintf(moveText + n, moveSize - n, "%s[%.2f,%.2f,%.2f]", k ? "," : "", 100 * m.track[k][0],
                  100 * m.track[k][1], 100 * m.track[k][2]);
  n += snprintf(moveText + n, moveSize - n, "]}");
  ws.textAll(moveText, n);
}

// Tables, spacings, every table's fingerprint, the last scan with its match
// per table, and the names of the networks.
void publishLine(const line::Tracker &t) {
  if (!lineText || ws.count() == 0) return;
  const int stations = t.learned.stations, room = lineSize;
  int n = snprintf(lineText, room, "{\"t\":\"ln\",\"n\":%d,\"cur\":%d,\"k\":%d,\"fix\":%d,\"gaps\":[", stations,
                   t.station(), t.known() ? 1 : 0, t.lastCorrected() ? 1 : 0);
  for (int i = 0; i < stations - 1; ++i) n += snprintf(lineText + n, room - n, "%s%.2f", i ? "," : "", t.gap(i));
  // Floor direction of each gap in degrees, null while not learned.
  n += snprintf(lineText + n, room - n, "],\"gdir\":[");
  for (int i = 0; i < stations - 1; ++i) {
    if (t.learned.gapDirKnown[i]) n += snprintf(lineText + n, room - n, "%s%.1f", i ? "," : "", t.learned.gapDir[i] * 57.29578f);
    else n += snprintf(lineText + n, room - n, "%snull", i ? "," : "");
  }
  n += snprintf(lineText + n, room - n, "],\"fp\":[");
  for (int s = 0; s < stations && n < room - 600; ++s) {
    if (s) lineText[n++] = ',';
    n += appendFingerprint(lineText + n, room - n, t.learned.fp[s]);
  }
  n += snprintf(lineText + n, room - n, "],\"scan\":");
  n += appendFingerprint(lineText + n, room - n, t.lastScan());
  n += snprintf(lineText + n, room - n, ",\"score\":[");
  for (int s = 0; s < stations; ++s) n += snprintf(lineText + n, room - n, "%s%.1f", s ? "," : "", t.score(s));
  n += snprintf(lineText + n, room - n, "],\"names\":{");
  // Names of every access point mentioned above, once.
  bool first = true;
  const auto name = [&](const line::Fingerprint &f) {
    for (int i = 0; i < f.count && n < room - 80; ++i) {
      const uint8_t *b = f.ap[i].bssid;
      char key[13];
      snprintf(key, sizeof key, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
      lineText[n] = 0;
      if (strstr(lineText, "\"names\":{") && strstr(strstr(lineText, "\"names\":{"), key)) continue;
      const char *ssidText = wifiscan::ssidOf(b);
      char clean[33];
      int c = 0;
      for (; ssidText[c] && c < 32; ++c) clean[c] = (ssidText[c] == '"' || ssidText[c] == '\\') ? '_' : ssidText[c];
      clean[c] = 0;
      n += snprintf(lineText + n, room - n, "%s\"%s\":\"%s\"", first ? "" : ",", key, clean);
      first = false;
    }
  };
  for (int s = 0; s < stations; ++s) name(t.learned.fp[s]);
  name(t.lastScan());
  n += snprintf(lineText + n, room - n, "}}");
  ws.textAll(lineText, n);
}

// Envelope as flat [lo x, lo y, lo z, hi x, hi y, hi z, ...] in 0.01 m/s2.
void publishTransit(const line::Transit &tr) {
  if (!transitText || ws.count() == 0) return;
  const int room = transitSize;
  int n = snprintf(transitText, room,
                   "{\"t\":\"tr\",\"id\":%lu,\"from\":%d,\"to\":%d,\"d\":%.2f,\"dur\":%.2f,\"dir\":%.0f,\"ramp\":%.3f,"
                   "\"rms\":[%.3f,%.3f,%.3f],\"pk\":[%.3f,%.3f,%.3f],\"wifi\":%d,\"carried\":%d,\"bin\":%.3f,\"env\":[",
                   (unsigned long)tr.id, tr.from, tr.to, tr.distance, tr.durationS, tr.floorDeg, tr.ramp, tr.rms[0], tr.rms[1],
                   tr.rms[2], tr.peak[0], tr.peak[1], tr.peak[2], tr.wifiCorrected ? 1 : 0, tr.carried ? 1 : 0, tr.binS);
  for (int b = 0; b < tr.bins && n < room - 60; ++b)
    n += snprintf(transitText + n, room - n, "%s%d,%d,%d,%d,%d,%d", b ? "," : "", tr.lo[b][0], tr.lo[b][1],
                  tr.lo[b][2], tr.hi[b][0], tr.hi[b][1], tr.hi[b][2]);
  n += snprintf(transitText + n, room - n, "]}");
  ws.textAll(transitText, n);
}

Command takeCommand(int &arg) {
  const Command c = Command(command.exchange(uint8_t(Command::None)));
  arg = commandArg;
  return c;
}
int clients() { return ws.count(); }
const char *address() { return ip; }
} // namespace weblink
