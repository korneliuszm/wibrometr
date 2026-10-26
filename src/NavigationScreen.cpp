#include "NavigationScreen.h"
#include <cmath>
#include "UiTheme.h"

namespace {

// Square map on the left (full display height), text panel on the right.
constexpr int mapSize = 135, mapMargin = 7, panelX = 140;

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void label(GFXcanvas16 &c, int x, int y, uint16_t color, const char *text) {
  c.setCursor(x, y);
  c.setTextColor(color);
  c.print(text);
}

struct MapView {
  float minX = 0, minY = 0, scale = 1;
  // World (metres, y up) to screen (pixels, y down), equal scale on both axes.
  int px(float x) const { return mapMargin + int(lroundf((x - minX) * scale)); }
  int py(float y) const { return mapSize - 1 - mapMargin - int(lroundf((y - minY) * scale)); }
};

// Fits the reference points (plus 12% padding, so a position on a corner
// stays visible) into the square; the shorter axis is centred.
MapView mapView(const NavScreenState &s) {
  float lo[2] = {s.refX[0], s.refY[0]}, hi[2] = {s.refX[0], s.refY[0]};
  for (int i = 1; i < 3; ++i) {
    lo[0] = fminf(lo[0], s.refX[i]);
    hi[0] = fmaxf(hi[0], s.refX[i]);
    lo[1] = fminf(lo[1], s.refY[i]);
    hi[1] = fmaxf(hi[1], s.refY[i]);
  }
  const float extent = fmaxf(fmaxf(hi[0] - lo[0], hi[1] - lo[1]), 0.1f);
  const float span = extent * 1.24f;
  MapView v;
  v.scale = (mapSize - 1 - 2 * mapMargin) / span;
  v.minX = (lo[0] + hi[0]) / 2 - span / 2;
  v.minY = (lo[1] + hi[1]) / 2 - span / 2;
  return v;
}

void drawMap(GFXcanvas16 &c, const NavScreenState &s, uint32_t now) {
  const MapView v = mapView(s);
  const auto &progress = s.progress;
  const auto &pos = s.position;
  c.drawRect(0, 0, mapSize, mapSize, ui::muted);
  // 1 m grid (2 m or 5 m on large areas), clipped to the map frame.
  const float span = (mapSize - 1 - 2 * mapMargin) / v.scale;
  const float step = span > 25 ? 5 : span > 12 ? 2 : 1;
  for (float g = ceilf(v.minX / step) * step; g <= v.minX + span; g += step) {
    const int x = v.px(g);
    if (x > 0 && x < mapSize - 1) c.drawFastVLine(x, 1, mapSize - 2, ui::grid);
  }
  for (float g = ceilf(v.minY / step) * step; g <= v.minY + span; g += step) {
    const int y = v.py(g);
    if (y > 0 && y < mapSize - 1) c.drawFastHLine(1, y, mapSize - 2, ui::grid);
  }
  int rx[3], ry[3];
  for (int i = 0; i < 3; ++i) {
    rx[i] = v.px(s.refX[i]);
    ry[i] = v.py(s.refY[i]);
  }
  for (int i = 0; i < 3; ++i) c.drawLine(rx[i], ry[i], rx[(i + 1) % 3], ry[(i + 1) % 3], ui::edge);
  const int labelDx[3] = {-11, 9, -2}, labelDy[3] = {3, 3, -15}; // Clear of the target ring.
  for (int i = 0; i < 3; ++i) {
    uint16_t color = s.calibrated ? ui::white : ui::muted;
    if (progress.active) {
      color = i < progress.point ? ui::green : i == progress.point ? ui::yellow : ui::muted;
      // Blink a ring around the point the user should walk to.
      if (i == progress.point && (progress.measuring || (now / 400) % 2))
        c.drawCircle(rx[i], ry[i], 7, ui::yellow);
    }
    c.fillCircle(rx[i], ry[i], 3, color);
    const char name[2] = {char('A' + i), 0};
    label(c, clampi(rx[i] + labelDx[i], 2, mapSize - 8), clampi(ry[i] + labelDy[i], 2, mapSize - 10), color, name);
  }
  if (progress.active || !pos.valid) return;
  const auto clampPx = [](int p) { return clampi(p, 3, mapSize - 4); };
  // Last unfiltered fix as a faint ring, filtered position as the main dot.
  c.drawCircle(clampPx(v.px(pos.rawXM)), clampPx(v.py(pos.rawYM)), 2, ui::muted);
  const uint16_t dot = pos.status == ips::PositionStatus::Ok ? ui::green :
                       pos.status == ips::PositionStatus::LowConfidence ? ui::yellow : ui::muted;
  const int dx = clampPx(v.px(pos.xM)), dy = clampPx(v.py(pos.yM));
  c.fillCircle(dx, dy, 5, dot);
  c.drawCircle(dx, dy, 6, ui::white);
}

void drawPanel(GFXcanvas16 &c, const NavScreenState &s, uint32_t now) {
  const auto &progress = s.progress;
  const auto &pos = s.position;
  label(c, panelX, 0, ui::white, "INDOOR NAV");
  c.setTextColor(ui::muted);
  c.setCursor(222, 0);
  c.printf("%d/%d", s.screenPos + 1, s.screenCount);
  const char *hint1, *hint2;
  if (progress.active) {
    const char point = char('A' + progress.point);
    label(c, panelX, 14, ui::yellow, "KALIBRACJA");
    c.setTextColor(ui::white);
    if (progress.measuring) {
      c.setCursor(panelX, 28);
      c.printf("Kalibracja %c:", point);
      c.setTextSize(2);
      c.setCursor(panelX, 40);
      c.printf("%u/%u", progress.scansDone, progress.scansTotal);
      c.setTextSize(1);
      const int w = 96;
      c.drawRect(panelX, 60, w, 7, ui::muted);
      c.fillRect(panelX + 1, 61, (w - 2) * progress.scansDone / progress.scansTotal, 5, ui::yellow);
      label(c, panelX, 74, ui::muted, "stoj w punkcie,");
      label(c, panelX, 84, ui::muted, "nie zaslaniaj");
      hint1 = "trwa pomiar...";
      hint2 = "1s: anuluj";
    } else {
      label(c, panelX, 28, ui::white, "Przejdz do");
      c.setTextSize(2);
      c.setCursor(panelX, 40);
      c.printf("punktu %c", point);
      c.setTextSize(1);
      c.setTextColor(ui::muted);
      c.setCursor(panelX, 60);
      c.printf("(%.1f, %.1f) m", s.refX[progress.point], s.refY[progress.point]);
      if (progress.lastPointFailed) {
        label(c, panelX, 74, ui::red, "Za malo sieci!");
        label(c, panelX, 84, ui::red, "powtorz pomiar");
      }
      hint1 = "BOOT: pomiar";
      hint2 = "1s: anuluj";
    }
  } else if (!s.calibrated) {
    label(c, panelX, 14, ui::yellow, "BRAK KALIBRACJI");
    label(c, panelX, 30, ui::muted, "Przytrzymaj BOOT");
    label(c, panelX, 40, ui::muted, "1 s, aby zaczac.");
    c.setCursor(panelX, 56);
    c.setTextColor(ui::muted);
    c.printf("Trojkat %.1f m", s.sideM);
    hint1 = "BOOT: ekran";
    hint2 = "1s: kalibracja";
  } else {
    const bool fix = pos.valid && pos.status != ips::PositionStatus::NoPosition;
    const char *state = !pos.updatedMs ? "SKANOWANIE..." :
                        pos.status == ips::PositionStatus::Ok ? "OK" :
                        pos.status == ips::PositionStatus::LowConfidence ? "SLABA JAKOSC" : "BRAK POZYCJI";
    label(c, panelX, 14, pos.status == ips::PositionStatus::Ok ? ui::green : ui::yellow, state);
    c.setTextSize(2);
    c.setTextColor(fix ? ui::white : ui::muted);
    c.setCursor(panelX, 28);
    if (pos.valid) c.printf("X %.1f", pos.xM); else c.print("X --");
    c.setCursor(panelX, 46);
    if (pos.valid) c.printf("Y %.1f", pos.yM); else c.print("Y --");
    c.setTextSize(1);
    label(c, panelX, 64, ui::muted, "m, szacunek");
    c.setTextColor(ui::white);
    c.setCursor(panelX, 76);
    c.printf("AP: %u", pos.apCount);
    c.setCursor(panelX, 86);
    c.printf("Conf: %u%%", pos.confidence);
    const int w = 96;
    c.drawRect(panelX, 96, w, 5, ui::muted);
    c.fillRect(panelX + 1, 97, (w - 2) * pos.confidence / 100, 3,
               pos.confidence >= ips::kOkConfidence ? ui::green : ui::yellow);
    if (pos.updatedMs) {
      c.setTextColor(ui::muted);
      c.setCursor(panelX, 104);
      c.printf("aktual. %lus temu", (unsigned long)((now - pos.updatedMs) / 1000));
    }
    hint1 = "BOOT: ekran";
    hint2 = "1s kal. 3s kasuj";
  }
  uint16_t hintColor = ui::muted;
  if (s.buttonHeld) {
    hintColor = ui::yellow;
    if (progress.active) {
      hint1 = s.heldMs >= 1000 ? "PUSC: ANULUJ" : progress.measuring ? "trwa pomiar..." : "PUSC: POMIAR";
      hint2 = "";
    } else {
      hint1 = s.heldMs >= 3000 ? "PUSC: USUN" : s.heldMs >= 1000 ? "PUSC: KALIBRACJA" : "PUSC: NAST. EKRAN";
      hint2 = s.heldMs >= 3000 ? "KALIBRACJE" : "";
    }
  } else if (s.notice) {
    hintColor = ui::yellow;
    hint1 = s.notice;
    hint2 = "";
  }
  label(c, panelX, 116, hintColor, hint1);
  label(c, panelX, 126, hintColor, hint2);
}

} // namespace

uint32_t navSignature(const NavScreenState &s, uint32_t now) {
  const auto &progress = s.progress;
  const auto &pos = s.position;
  uint32_t h = 2166136261u;
  const auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  mix(progress.active | progress.measuring << 1 | progress.lastPointFailed << 2 | s.calibrated << 3);
  mix(uint32_t(progress.point) << 8 | progress.scansDone);
  mix(pos.updatedMs);
  mix(pos.updatedMs ? (now - pos.updatedMs) / 1000 : 0); // "N s ago" counter.
  mix(progress.active && !progress.measuring ? now / 400 : 0); // Blinking target.
  mix(s.buttonHeld ? 1 + (s.heldMs >= 1000) + (s.heldMs >= 3000) : 0);
  mix(uint32_t(uintptr_t(s.notice)));
  mix(uint32_t(s.screenPos));
  return h;
}

void drawNavigationScreen(GFXcanvas16 &canvas, const NavScreenState &s, uint32_t now) {
  canvas.fillScreen(ui::bg);
  canvas.setTextSize(1);
  drawMap(canvas, s, now);
  drawPanel(canvas, s, now);
}
