#include "ui/LineScreens.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "ui/Draw.h"

namespace ui {

namespace {

using State = DisplacementMeter::State;
constexpr int W = 320;
constexpr float kRadToDeg = 57.29578f;

// One line under the header: what the displacement meter is doing right now.
void meterStatus(GFXcanvas16 &c, const Model &m, int y) {
  const DisplacementMeter &d = *m.meter;
  if (!m.live) return text(c, 0, y, 1, ui::yellow, m.sensorStatus);
  switch (d.state()) {
  case State::Settling: text(c, 0, y, 1, ui::yellow, "STABILIZACJA - NIE RUSZAJ"); break;
  case State::TooLong: text(c, 0, y, 1, ui::yellow, "RUCH > 15 s - POMIAR PORZUCONY"); break;
  case State::Moving: textf(c, 0, y, 1, ui::yellow, "RUCH %.1f s", d.movingS()); break;
  case State::Rest:
    if (d.moves() && d.last().tilted) text(c, 0, y, 1, ui::yellow, "BLAD ORIENTACJI: WYNIK NIEPEWNY");
    else textf(c, 0, y, 1, ui::green, "SPOCZYNEK  ruchow: %lu", (unsigned long)d.moves());
    break;
  }
}

void noMove(GFXcanvas16 &c) {
  text(c, 60, 90, 2, ui::muted, "BRAK RUCHU");
  text(c, 60, 114, 1, ui::muted, "przesun czujnik i zostaw go nieruchomo");
}

void plotGrid(GFXcanvas16 &c, int x, int y, int w, int h) {
  for (int n = 0; n <= 4; ++n) {
    c.drawFastHLine(x, y + n * (h - 1) / 4, w, ui::grid);
    c.drawFastVLine(x + n * (w - 1) / 4, y, h, ui::grid);
  }
}

} // namespace

// ------------------------------------------------------------------- LINIA

void drawLinePage(GFXcanvas16 &c, const Model &m) {
  if (!m.tracker || !m.configOk) { // Nothing is tracked without the line's description.
    text(c, 0, 30, 2, ui::red, "BRAK PRAWIDLOWEJ");
    text(c, 0, 50, 2, ui::red, "KONFIGURACJI");
    if (m.configLine) textf(c, 0, 80, 1, ui::yellow, "config.csv, wiersz %d:", m.configLine);
    else text(c, 0, 80, 1, ui::yellow, "config.csv:");
    text(c, 0, 94, 1, ui::yellow, m.configWhat);
    text(c, 0, 118, 1, ui::muted, "Wgraj config.csv na dysk USB WIBROMTR");
    text(c, 0, 130, 1, ui::muted, "(podlacz kabel USB-C) i nacisnij RESET.");
    return;
  }
  const line::Tracker &t = *m.tracker;
  const int n = t.stations();
  const auto phase = t.phase();
  const bool moving = phase == line::Tracker::Phase::Moving && !t.arrived();
  // Big state at the left: where the part is.
  if (!m.live) {
    text(c, 0, 30, 2, ui::yellow, m.sensorStatus);
  } else if (t.carrying() && !t.arrived()) {
    text(c, 0, 28, 4, ui::yellow, "NIESIONY");
  } else if (!t.known()) {
    text(c, 0, 28, 4, ui::yellow, t.lost() == line::Tracker::Lost::Carried ? "PRZENIES." : "STOL ?");
  } else if (moving) {
    textf(c, 0, 28, 4, ui::yellow, "%d>%d", t.station() + 1, t.station() + 1 + t.direction());
  } else {
    const int s = t.arrived() ? int(lroundf(t.position())) : t.station();
    const line::StationRow *row = t.stationRow(s);
    if (row && row->id[0]) { // Name from config.csv; size 4 fits 8 characters.
      textf(c, 0, 28, std::strlen(row->id) > 8 ? 2 : 4, ui::white, "%s", row->id);
    } else textf(c, 0, 28, 4, ui::white, "STOL %d", s + 1);
  }
  textf(c, 210, 28, 1, ui::muted, "z %d stolow", n);
  if (moving) textf(c, 210, 42, 1, ui::muted, "%.1f m", t.liveDistance());
  else if (phase != line::Tracker::Phase::Settling) {
    const int s = int(t.phaseS());
    textf(c, 210, 42, 1, ui::muted, "postoj %d:%02d", s / 60, s % 60);
  }
  // Heading on the floor (0 = as at table 1) and a turn in progress.
  if (t.turning()) textf(c, 210, 56, 1, ui::yellow, "obrot %+.0f st", ui::shown(t.turnSoFar(), 2) * kRadToDeg);
  else if (t.headingKnown()) textf(c, 210, 56, 1, ui::muted, "kurs %+.0f st", ui::shown(t.heading(), 2) * kRadToDeg);
  // Track: tables evenly spread, the part at its fractional position.
  const int y = 104, x0 = 14, x1 = W - 14;
  c.drawFastHLine(x0, y + 12, x1 - x0, ui::muted);
  const float span = n > 1 ? float(x1 - x0) / (n - 1) : 0.0f;
  for (int i = 0; i < n - 1; ++i) // Lift shafts: that piece of the track in cyan.
    if (t.shaft(i)) c.fillRect(x0 + int(lroundf(i * span)), y + 11, int(lroundf(span)) + 1, 3, 0x07FF);
  const int half = std::max(1, std::min(10, int(span / 2) - 1)); // Boxes narrow down with many tables.
  const int every = (n + 11) / 12;                               // At most ~12 numbers under the track.
  for (int s = 0; s < n; ++s) {
    const int x = x0 + int(lroundf(s * span));
    const bool here = t.known() && !moving && s == t.station();
    const bool fp = t.learned.fp[s].count > 0;
    c.fillRect(x - half, y, 2 * half + 1, 11, here ? ui::green : (fp ? ui::grid : ui::bg));
    c.drawRect(x - half, y, 2 * half + 1, 11, here ? ui::green : ui::muted);
    if (s % every == 0) textf(c, x - (s >= 9 ? 6 : 3), y + 18, 1, here ? ui::green : ui::muted, "%d", s + 1);
  }
  if (t.known() && m.live) {
    const int px = x0 + int(lroundf(t.position() * span));
    c.fillTriangle(px - 7, y - 16, px + 7, y - 16, px, y - 4, moving ? ui::yellow : ui::white);
  }
  // Wi-Fi fingerprint progress.
  const float wait = t.untilPark();
  if (m.scanBusy) {
    text(c, 0, 142, 1, ui::yellow, "skan Wi-Fi (odcisk stolu)...");
  } else if (wait >= 0) {
    textf(c, 0, 142, 1, ui::muted, "odcisk Wi-Fi za %.0f s", std::ceil(wait));
  } else {
    int fps = 0;
    for (int s = 0; s < n; ++s) fps += t.learned.fp[s].count > 0;
    textf(c, 0, 142, 1, ui::muted, "odciski Wi-Fi %d/%d  przejazdy %d%s", fps, n, t.transitCount(),
          t.lastCorrected() ? "  stol z Wi-Fi" : "");
  }
  // Ride detector against its threshold, which follows the shaking.
  const bool over = t.activity() > t.startThreshold();
  textf(c, 0, 158, 1, over ? ui::yellow : ui::muted, "ruch %.3f / prog %.3f m/s  drg %.2f", t.activity(),
        t.startThreshold(), t.shaking());
}

// ---------------------------------------------------------------- ZYROSKOP

void drawGyroPage(GFXcanvas16 &c, const Model &m) {
  constexpr float fullScale = 200.0f; // Bar end: +-200 deg/s.
  constexpr int rowY[3] = {24, 74, 124};
  constexpr int barMid = W / 2, barHalf = 156;
  for (int i = 0; i < 3; ++i) {
    const float v = component(m.gyroRate, i);
    const int y = rowY[i];
    text(c, 2, y, 4, kAxisColor[i], i == 0 ? "X" : i == 1 ? "Y" : "Z");
    if (m.live) textf(c, 34, y, 4, ui::white, "%+7.1f", v);
    else text(c, 34, y, 4, ui::muted, "    -- ");
    text(c, 224, y + 2, 2, ui::muted, "deg/s");
    if (m.live) textf(c, 224, y + 20, 1, ui::white, "kat %+.1f", component(m.gyroAngle, i));
    else text(c, 224, y + 20, 1, ui::muted, "kat --");
    const int by = y + 36;
    c.drawFastHLine(0, by + 4, W, ui::grid);
    c.drawFastVLine(barMid, by, 9, ui::muted);
    if (m.live) {
      const int len = int(lroundf(fmaxf(-1.0f, fminf(1.0f, v / fullScale)) * barHalf));
      if (len >= 0) c.fillRect(barMid, by + 1, len + 1, 7, kAxisColor[i]);
      else c.fillRect(barMid + len, by + 1, -len, 7, kAxisColor[i]);
    }
  }
  if (!m.live) text(c, 0, 174, 1, ui::yellow, m.sensorStatus);
  else if (!m.gyroBiasKnown) text(c, 0, 174, 1, ui::yellow, "POLOZ NIERUCHOMO: POMIAR OFFSETU");
  else {
    const Accel &r = m.gyroRate;
    textf(c, 0, 174, 1, ui::muted, "|w| %.1f deg/s  offset z spoczynku", std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z));
  }
}

// --------------------------------------------------------- PRZEMIESZCZENIE

void drawDisplacementPage(GFXcanvas16 &c, const Model &m) {
  const DisplacementMeter &d = *m.meter;
  meterStatus(c, m, 28);
  const bool moving = m.live && d.state() == State::Moving;
  const bool have = moving || d.moves();
  constexpr int rowY[3] = {44, 88, 132};
  for (int i = 0; i < 3; ++i) {
    const int y = rowY[i];
    const float cm = 100 * ui::shown(moving ? d.live(i) : d.last().d[i], i);
    text(c, 2, y, 4, kAxisColor[i], i == 0 ? "X" : i == 1 ? "Y" : "Z");
    if (have) textf(c, 34, y, 4, moving ? ui::yellow : ui::white, "%+7.1f", cm);
    else text(c, 34, y, 4, ui::muted, "    -- ");
    text(c, 232, y + 2, 2, ui::muted, "cm");
    textf(c, 232, y + 20, 1, ui::muted, "suma %+.1f", 100 * ui::shown(d.total(i), i));
  }
  if (moving) text(c, 0, 178, 1, ui::muted, "w trakcie: szacunek bez korekty dryfu");
  else if (d.moves()) {
    const auto &v = d.last().d;
    textf(c, 0, 178, 1, ui::muted, "|d| %.1f cm  %.1f s  %.2f m/s  %.0f st", 100 * std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]),
          d.last().durationS, d.last().peakSpeed, d.last().rotationDeg);
  } else text(c, 0, 178, 1, ui::muted, "ruch od spoczynku do spoczynku");
}

void drawDisplacementPlotPage(GFXcanvas16 &c, const Model &m) {
  constexpr int plotX = 34, plotY = 44, plotW = 272, plotH = 124;
  const DisplacementMeter &d = *m.meter;
  const auto &mv = d.last();
  meterStatus(c, m, 28);
  text(c, 230, 28, 1, kAxisColor[0], "X");
  text(c, 242, 28, 1, kAxisColor[1], "Y");
  text(c, 254, 28, 1, kAxisColor[2], "Z");
  text(c, 270, 28, 1, ui::muted, "cm");
  plotGrid(c, plotX, plotY, plotW, plotH);
  if (!d.moves() || mv.points < 2) return noMove(c);
  float peak = 0;
  for (int k = 0; k < mv.points; ++k)
    for (int i = 0; i < 3; ++i) peak = fmaxf(peak, fabsf(100 * mv.track[k][i]));
  const float range = niceAtLeast(fmaxf(peak, 0.5f)); // cm at the top and bottom edge.
  const auto yOf = [&](float metres) {
    const float v = fmaxf(-range, fminf(range, 100 * metres));
    return plotY + int(lroundf((range - v) * (plotH - 1) / (2 * range)));
  };
  for (int i = 0; i < 3; ++i) {
    int px = 0, py = 0;
    for (int k = 0; k < mv.points; ++k) {
      const int x = plotX + k * (plotW - 1) / (mv.points - 1);
      const int y = yOf(ui::shown(mv.track[k][i], i));
      if (k) c.drawLine(px, py, x, y, kAxisColor[i]);
      px = x;
      py = y;
    }
  }
  textf(c, 0, plotY, 1, ui::muted, "%+g", range);
  text(c, 18, plotY + plotH / 2 - 3, 1, ui::muted, "0");
  textf(c, 0, plotY + plotH - 8, 1, ui::muted, "%+g", -range);
  text(c, plotX, plotY + plotH + 4, 1, ui::muted, "0 s");
  textf(c, plotX + plotW - 36, plotY + plotH + 4, 1, ui::muted, "%.1f s", mv.durationS);
}

void drawDisplacementPathPage(GFXcanvas16 &c, const Model &m) {
  const DisplacementMeter &d = *m.meter;
  const auto &mv = d.last();
  meterStatus(c, m, 28);
  if (!d.moves() || mv.points < 2) return noMove(c);
  // Plane of the two axes the move spans most, in X-Y-Z order.
  float lo[3], hi[3], span[3];
  for (int i = 0; i < 3; ++i) {
    lo[i] = hi[i] = 0;
    for (int k = 0; k < mv.points; ++k) {
      lo[i] = fminf(lo[i], ui::shown(mv.track[k][i], i));
      hi[i] = fmaxf(hi[i], ui::shown(mv.track[k][i], i));
    }
    span[i] = hi[i] - lo[i];
  }
  const int smallest = span[0] <= span[1] && span[0] <= span[2] ? 0 : span[1] <= span[2] ? 1 : 2;
  const int h = smallest == 0 ? 1 : 0, v = smallest == 2 ? 1 : 2;
  textf(c, 150, 28, 1, ui::muted, "rzut %c-%c", kAxisLetter[h], kAxisLetter[v]);
  // Same scale on both axes so the shape is true.
  constexpr int areaX = 34, areaY = 44, areaW = 272, areaH = 124;
  const float spanH = fmaxf(span[h], 0.01f), spanV = fmaxf(span[v], 0.01f);
  const float scale = 0.9f * fminf(areaW / spanH, areaH / spanV); // px per metre
  const float cx = areaX + areaW / 2.0f, cy = areaY + areaH / 2.0f;
  const float mh = (lo[h] + hi[h]) / 2, mvv = (lo[v] + hi[v]) / 2;
  const auto px = [&](float x) { return int(lroundf(cx + (x - mh) * scale)); };
  const auto py = [&](float y) { return int(lroundf(cy - (y - mvv) * scale)); };
  // Grid at a 1-2-5 step of at least 15 px, through the start point.
  const float step = niceAtLeast(15 / scale * 100) / 100; // m
  for (int n = -30; n <= 30; ++n) {
    const int x = px(n * step), y = py(n * step);
    if (x >= areaX && x < areaX + areaW) c.drawFastVLine(x, areaY, areaH, n ? ui::grid : ui::edge);
    if (y >= areaY && y < areaY + areaH) c.drawFastHLine(areaX, y, areaW, n ? ui::grid : ui::edge);
  }
  for (int k = 1; k < mv.points; ++k)
    c.drawLine(px(ui::shown(mv.track[k - 1][h], h)), py(ui::shown(mv.track[k - 1][v], v)), px(ui::shown(mv.track[k][h], h)),
               py(ui::shown(mv.track[k][v], v)), ui::yellow);
  c.drawCircle(px(0), py(0), 3, ui::green);
  c.fillCircle(px(ui::shown(mv.track[mv.points - 1][h], h)), py(ui::shown(mv.track[mv.points - 1][v], v)), 3, ui::red);
  textf(c, areaX + areaW - 14, areaY + areaH - 10, 1, kAxisColor[h], "%c>", kAxisLetter[h]);
  textf(c, areaX - 16, areaY, 1, kAxisColor[v], "%c^", kAxisLetter[v]);
  textf(c, 0, areaY + areaH + 6, 1, ui::muted, "siatka %g cm   o start   * koniec", 100 * step);
}

// ----------------------------------------------------------- SIEC I WWW

void drawWebPage(GFXcanvas16 &c, const Model &m) {
  if (!m.network) return;
  const net::Status &s = *m.network;
  text(c, 0, 28, 1, ui::muted, "Siec:");
  textf(c, 40, 24, 2, ui::white, "%.20s", s.ssid[0] ? s.ssid : "--");
  textf(c, 0, 46, 1, s.state == net::State::Connected ? ui::green : ui::yellow, "%.52s", s.message);
  if (s.errorLine) textf(c, 0, 58, 1, ui::muted, "wifi.txt: wiersz %d", s.errorLine);
  else if (s.state == net::State::Disconnected) textf(c, 0, 58, 1, ui::muted, "powod Wi-Fi: %d", s.disconnectReason);
  textf(c, 0, 76, 2, ui::white, "IP: %s", s.ip[0] ? s.ip : "--");
  textf(c, 0, 100, 1, ui::green, "http://%.40s.local", s.hostname);
  textf(c, 0, 118, 1, ui::muted, "Zegar: %s", s.clockReady ? "NTP OK" : "brak czasu NTP");
  textf(c, 0, 132, 1, ui::muted, "Otwarte strony: %d", m.webClients);
  text(c, 0, 156, 1, ui::muted, "wifi.txt i config.csv: dysk USB WIBROMTR");
}

} // namespace ui
