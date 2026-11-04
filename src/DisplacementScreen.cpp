#include "DisplacementScreen.h"
#include <cmath>
#include "UiTheme.h"

namespace {
using State = DisplacementMeter::State;
constexpr int rowY[3] = {24, 52, 80};
const uint16_t axisColor[3] = {ui::red, ui::green, ui::blue};
const char axisLetter[3] = {'X', 'Y', 'Z'};
constexpr int plotX = 30, plotY = 24, plotW = 200, plotH = 82;

void header(GFXcanvas16 &c, const DisplacementScreenState &s, const char *title) {
  c.setTextSize(1);
  c.setCursor(0, 0);
  c.setTextColor(ui::white);
  c.print(title);
  c.setCursor(162, 0);
  c.printf("%3.0f Hz", s.live ? s.rateHz : 0);
  c.setTextColor(ui::muted);
  c.setCursor(222, 0);
  c.printf("%d/%d", s.screenPos + 1, s.screenCount);
}
// One line under the header: what the meter is doing right now.
void status(GFXcanvas16 &c, const DisplacementScreenState &s, int y) {
  const DisplacementMeter &m = *s.meter;
  c.setTextSize(1);
  c.setCursor(0, y);
  if (!s.live) { c.setTextColor(ui::yellow); c.print(s.status); return; }
  switch (m.state()) {
    case State::Settling: c.setTextColor(ui::yellow); c.print("STABILIZACJA - NIE RUSZAJ"); break;
    case State::TooLong: c.setTextColor(ui::yellow); c.print("RUCH > 15 s - POMIAR PORZUCONY"); break;
    case State::Moving: c.setTextColor(ui::yellow); c.printf("RUCH %.1f s", m.movingS()); break;
    case State::Rest:
      if (m.moves() && m.last().tilted) { c.setTextColor(ui::yellow); c.print("BLAD ORIENTACJI: WYNIK NIEPEWNY"); }
      else { c.setTextColor(ui::green); c.printf("SPOCZYNEK  ruchow: %lu", (unsigned long)m.moves()); }
      break;
  }
}
// Smallest 1-2-5 step of at least `v`.
float niceAtLeast(float v) {
  float decade = 1;
  while (decade * 10 <= v) decade *= 10;
  while (decade > v) decade /= 10;
  for (float k : {1.0f, 2.0f, 5.0f, 10.0f})
    if (k * decade >= v) return k * decade;
  return 10 * decade;
}
void noMove(GFXcanvas16 &c) {
  c.setTextColor(ui::muted);
  c.setCursor(40, 60);
  c.print("BRAK RUCHU - przesun czujnik");
  c.setCursor(40, 72);
  c.print("i zostaw go nieruchomo");
}
} // namespace

void drawDisplacementScreen(GFXcanvas16 &c, const DisplacementScreenState &s) {
  const DisplacementMeter &m = *s.meter;
  c.fillScreen(ui::bg);
  header(c, s, "PRZEMIESZCZENIE");
  status(c, s, 12);
  const bool moving = s.live && m.state() == State::Moving;
  const bool have = moving || m.moves();
  for (int i = 0; i < 3; ++i) {
    const int y = rowY[i];
    const float cm = 100 * ui::shown(moving ? m.live(i) : m.last().d[i], i);
    c.setTextSize(3);
    c.setTextColor(axisColor[i]);
    c.setCursor(0, y);
    c.printf("%c", axisLetter[i]);
    c.setTextColor(moving ? ui::yellow : have ? ui::white : ui::muted);
    c.setCursor(22, y);
    if (have) c.printf("%+6.1f", cm); else c.print("   -- ");
    c.setTextSize(1);
    c.setTextColor(ui::muted);
    c.setCursor(136, y + 3);
    c.print("cm");
    c.setCursor(136, y + 14);
    c.printf("suma %+.1f", 100 * m.total(i));
  }
  c.setTextSize(1);
  c.setCursor(0, 108);
  c.setTextColor(ui::muted);
  if (moving) c.print("w trakcie: szacunek bez korekty dryfu");
  else if (m.moves()) {
    const auto &d = m.last().d;
    c.printf("|d| %.1f cm  %.1f s  %.2f m/s  %.0f st", 100 * sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]),
             m.last().durationS, m.last().peakSpeed, m.last().rotationDeg);
  } else c.print("ruch od spoczynku do spoczynku");
}

void drawDisplacementPlot(GFXcanvas16 &c, const DisplacementScreenState &s) {
  const DisplacementMeter &m = *s.meter;
  const auto &mv = m.last();
  c.fillScreen(ui::bg);
  header(c, s, "RUCH W CZASIE");
  c.setTextColor(ui::red); c.setCursor(86, 0); c.print("X");
  c.setTextColor(ui::green); c.setCursor(98, 0); c.print("Y");
  c.setTextColor(ui::blue); c.setCursor(110, 0); c.print("Z");
  c.setTextColor(ui::muted); c.setCursor(124, 0); c.print("cm");
  status(c, s, 12);
  for (int n = 0; n <= 4; ++n) {
    c.drawFastHLine(plotX, plotY + n * (plotH - 1) / 4, plotW, ui::grid);
    c.drawFastVLine(plotX + n * (plotW - 1) / 4, plotY, plotH, ui::grid);
  }
  if (!m.moves() || mv.points < 2) { noMove(c); return; }
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
      if (k) c.drawLine(px, py, x, y, axisColor[i]);
      px = x; py = y;
    }
  }
  c.setTextColor(ui::muted);
  c.setCursor(0, plotY);
  c.printf("%+g", range);
  c.setCursor(18, plotY + plotH / 2 - 3);
  c.print("0");
  c.setCursor(0, plotY + plotH - 7);
  c.printf("%+g", -range);
  c.setCursor(plotX, 110);
  c.print("0 s");
  c.setCursor(198, 110);
  c.printf("%.1f s", mv.durationS);
}

void drawDisplacementPath(GFXcanvas16 &c, const DisplacementScreenState &s) {
  const DisplacementMeter &m = *s.meter;
  const auto &mv = m.last();
  c.fillScreen(ui::bg);
  header(c, s, "TOR RUCHU");
  status(c, s, 12);
  if (!m.moves() || mv.points < 2) { noMove(c); return; }
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
  c.setTextColor(ui::muted);
  c.setCursor(80, 0);
  c.printf("rzut %c-%c", axisLetter[h], axisLetter[v]);
  // Same scale on both axes so the shape is true.
  constexpr int areaX = 30, areaY = 24, areaW = 200, areaH = 84;
  const float spanH = fmaxf(span[h], 0.01f), spanV = fmaxf(span[v], 0.01f);
  const float scale = 0.9f * fminf(areaW / spanH, areaH / spanV); // px per metre
  const float cx = areaX + areaW / 2.0f, cy = areaY + areaH / 2.0f;
  const float mh = (lo[h] + hi[h]) / 2, mvv = (lo[v] + hi[v]) / 2;
  const auto px = [&](float x) { return int(lroundf(cx + (x - mh) * scale)); };
  const auto py = [&](float y) { return int(lroundf(cy - (y - mvv) * scale)); };
  // Grid at a 1-2-5 step of at least 15 px, through the start point.
  const float step = niceAtLeast(15 / scale * 100) / 100; // m
  for (int n = -20; n <= 20; ++n) {
    const int x = px(n * step), y = py(n * step);
    if (x >= areaX && x < areaX + areaW) c.drawFastVLine(x, areaY, areaH, n ? ui::grid : ui::edge);
    if (y >= areaY && y < areaY + areaH) c.drawFastHLine(areaX, y, areaW, n ? ui::grid : ui::edge);
  }
  for (int k = 1; k < mv.points; ++k)
    c.drawLine(px(ui::shown(mv.track[k - 1][h], h)), py(ui::shown(mv.track[k - 1][v], v)), px(ui::shown(mv.track[k][h], h)),
               py(ui::shown(mv.track[k][v], v)), ui::yellow);
  c.drawCircle(px(0), py(0), 3, ui::green);
  c.fillCircle(px(ui::shown(mv.track[mv.points - 1][h], h)), py(ui::shown(mv.track[mv.points - 1][v], v)), 3, ui::red);
  c.setTextColor(axisColor[h]);
  c.setCursor(areaX + areaW - 6, areaY + areaH - 8);
  c.printf("%c>", axisLetter[h]);
  c.setTextColor(axisColor[v]);
  c.setCursor(areaX - 14, areaY);
  c.printf("%c^", axisLetter[v]);
  c.setTextColor(ui::muted);
  c.setCursor(0, 110);
  c.printf("siatka %g cm   o start   * koniec", 100 * step);
}
