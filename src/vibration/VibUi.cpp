#include "VibUi.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "TouchCalibration.h"

// Five screens of the vibration recorder (swipe sideways), 320x240, laid out for this panel
// (proportional FreeSans fonts, not the scaled 6x8 font of the Feather screens):
//
//   y0    status bar: state pill (CZUWAM / REC 12.3 / 30 s + progress / USB / ...), card, battery
//   y36   left 2/3: chart panel, XYZ dynamic acceleration (5.4 s),
//         bottom row: axis legend, live level
//         right 1/3: statistics panel of the last recorded measurement (mean, RMS,
//         peak, peak-to-peak per axis), below it the spectrum of the live signal (last ~2 s,
//         three axes overlaid, strongest line of the XYZ vector)
//   y196  range finder strip: distance now, bar with the trigger distance in the middle, state
//
// Page 2 (peaks of the session): the status bar, then one panel with a bar per
// recorded measurement (its largest XYZ vector length, dots: largest per axis);
// both axes scale to what has been recorded since power-up.
//
// Page 3 (FFT): the status bar, the spectra of the last measurement per sensor axis X, Y, Z
// in the left 2/3 (one below the other, frequency axis to Nyquist, one common
// amplitude scale, numbered markers on the strongest lines) and a table of the three
// strongest lines of each axis in the right 1/3.
//
// Page 4 (range finder): four rows [ - ] label / value [ + ] (rangeui::rowRect): trigger
// distance, recording time, pre-roll, measuring mode; the range finder strip below.
//
// Page 5: the status bar and three wide buttons: USB disk mode ("WLACZ TRYB DYSKU USB" / "ZAKONCZ TRYB
// DYSKU"; by default the recorder keeps the card, the computer sees it only in disk mode), touch
// calibration, and clearing the card of old measurements (asks twice).
//
// All text is Polish without diacritics (the GFX fonts have none).

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Palette.
constexpr uint16_t kBg = rgb(8, 11, 18);
constexpr uint16_t kPanel = rgb(16, 21, 32);
constexpr uint16_t kPanelEdge = rgb(42, 52, 72);
constexpr uint16_t kGrid = rgb(34, 42, 58);
constexpr uint16_t kZero = rgb(62, 74, 98);
constexpr uint16_t kText = rgb(235, 240, 248);
constexpr uint16_t kMuted = rgb(130, 142, 165);
constexpr uint16_t kDim = rgb(80, 90, 112);
constexpr uint16_t kAmber = rgb(255, 190, 50);
constexpr uint16_t kRed = rgb(240, 64, 72);
constexpr uint16_t kGreen = rgb(70, 220, 130);
constexpr uint16_t kBlue = rgb(110, 170, 255);
constexpr uint16_t kButton = rgb(32, 42, 60);
constexpr uint16_t kButtonPressed = rgb(70, 92, 128);
constexpr uint16_t kAxis[3] = {rgb(255, 96, 96), rgb(90, 225, 130), rgb(90, 170, 255)};  // X Y Z
constexpr char kAxisName[3] = {'X', 'Y', 'Z'};

// Layout.
constexpr int W = 320;
constexpr int kPanelX = 4, kPanelY = 36, kPanelW = 200, kPanelH = 152;  // Chart: left 2/3.
constexpr int kPlotX = 34, kPlotY = 44, kPlotW = 162, kPlotH = 126;
constexpr int kStatX = 208, kStatW = 108;  // Statistics: right 1/3, same top and height.
constexpr int kRowY = kPlotY + kPlotH + 6;  // Legend / level row inside the panel.

const GFXfont *const kSmall = &FreeSans9pt7b;
const GFXfont *const kBold = &FreeSansBold9pt7b;
const GFXfont *const kBig = &FreeSansBold18pt7b;

float component(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

// ---- Text helpers ----------------------------------------------------------------

// Draws with a GFX font; y is the baseline. Returns the x after the text.
int fontText(GFXcanvas16 &c, const GFXfont *f, int x, int baseline, uint16_t color, const char *s) {
  c.setFont(f);
  c.setTextSize(1);
  c.setTextColor(color);
  c.setCursor(x, baseline);
  c.print(s);
  return c.getCursorX();
}

int fontWidth(GFXcanvas16 &c, const GFXfont *f, const char *s) {
  c.setFont(f);
  c.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  c.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return int(w) + x1;
}

// Built-in 6x8 font for small labels; y is the top.
void small(GFXcanvas16 &c, int x, int y, uint16_t color, const char *s) {
  c.setFont(nullptr);
  c.setTextSize(1);
  c.setTextColor(color);
  c.setCursor(x, y);
  c.print(s);
}
int smallWidth(const char *s) { return int(std::strlen(s)) * 6; }

// ---- Status bar --------------------------------------------------------------------

struct Pill {
  uint16_t fill, color;
  bool dot, blink;
  char text[32];
};

// By priority:
//   1. USB (host owns the card, or the hand-over is still under way).
//   2. Recording (core Recording and the writer Idle/Recording; with NoCard/Error the
//      samples are discarded, so fall through).
//   3. Card problems.  4. No sensor.  5. Armed.
Pill statusPill(const VibModel &m) {
  Pill p{};
  if (m.hostOwns || m.mode == RecorderCore::Mode::Usb || m.sd.state == MeasStatus::State::Parked) {
    p = {rgb(22, 48, 96), kBlue, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "USB: KARTA W PC");
  } else if (m.mode == RecorderCore::Mode::Recording &&
             (m.sd.state == MeasStatus::State::Idle || m.sd.state == MeasStatus::State::Recording)) {
    p = {rgb(190, 28, 40), kText, true, true, ""};
    const uint32_t ms = m.sd.recordingMs;
    std::snprintf(p.text, sizeof p.text, "REC  %lu.%lu / %lu s", (unsigned long)(ms / 1000),
                  (unsigned long)(ms / 100 % 10), (unsigned long)m.range.recordS);
  } else if (m.sd.state == MeasStatus::State::Cleaning) {
    p = {rgb(84, 62, 10), kAmber, true, true, ""};
    std::snprintf(p.text, sizeof p.text, "CZYSZCZENIE");
  } else if (m.sd.state == MeasStatus::State::NoCard) {
    p = {rgb(84, 62, 10), kAmber, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BRAK KARTY");
  } else if (m.sd.state == MeasStatus::State::Error) {
    p = {rgb(96, 20, 26), rgb(255, 130, 130), false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BLAD KARTY");
  } else if (!m.sensorLive) {
    p = {rgb(84, 62, 10), kAmber, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BRAK CZUJNIKA");
  } else if (m.rangeState == VibModel::RangeState::Missing) {
    p = {rgb(84, 62, 10), kAmber, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BRAK DALMIERZA");
  } else {
    p = {rgb(16, 62, 38), kGreen, true, false, ""};
    std::snprintf(p.text, sizeof p.text, "CZUWAM");
  }
  return p;
}

void drawBattery(GFXcanvas16 &c, int right, const VibModel &m, int &left) {
  const battery::Reading &b = m.battery;
  if (b.state == battery::State::NoBattery) {
    const char *s = "USB";
    left = right - fontWidth(c, kBold, s);
    fontText(c, kBold, left, 23, kMuted, s);
    return;
  }
  // Icon: 24x13 body and a nub, fill by charge.
  const int bw = 24, bh = 13, bx = right - bw - 3, by = 9;
  const float pct = fminf(fmaxf(b.percent, 0.0f), 100.0f);
  const uint16_t color = m.charging ? kBlue : pct < 15 ? kRed : pct < 30 ? kAmber : kGreen;
  c.drawRoundRect(bx, by, bw, bh, 3, kMuted);
  c.fillRect(bx + bw, by + 4, 3, bh - 8, kMuted);
  const int fill = int(lroundf(pct * (bw - 4) / 100.0f));
  if (fill > 0) c.fillRect(bx + 2, by + 2, fill, bh - 4, color);
  if (m.charging) {  // Lightning bolt across the icon.
    const int cx = bx + bw / 2;
    c.fillTriangle(cx + 2, by + 1, cx - 4, by + 7, cx + 1, by + 7, kText);
    c.fillTriangle(cx - 1, by + 6, cx + 4, by + 6, cx - 2, by + bh - 1, kText);
  }
  char s[8];
  std::snprintf(s, sizeof s, "%.0f%%", pct);
  left = bx - 6 - fontWidth(c, kBold, s);
  fontText(c, kBold, left, 23, pct < 15 && !m.charging ? kRed : kText, s);
}

// Card icon and the number of measurements on it.
void drawCard(GFXcanvas16 &c, int right, const VibModel &m) {
  char s[12];
  const bool card = m.sd.cardMb > 0 && m.sd.state != MeasStatus::State::NoCard;
  if (card) std::snprintf(s, sizeof s, "%lu", (unsigned long)m.sd.measurements);
  else std::snprintf(s, sizeof s, "-");
  const int tx = right - fontWidth(c, kBold, s);
  fontText(c, kBold, tx, 23, card ? kText : kMuted, s);
  const int ix = tx - 15, iy = 8;  // 11x15 card with a cut corner.
  const uint16_t color = m.sd.recoveries ? kAmber : card ? kMuted : kDim;
  c.fillRoundRect(ix, iy, 11, 15, 2, color);
  c.fillTriangle(ix + 7, iy - 1, ix + 12, iy - 1, ix + 12, iy + 4, kBg);
  for (int i = 0; i < 3; ++i) c.drawFastVLine(ix + 2 + 2 * i, iy + 2, 4, kBg);
}

void drawStatusBar(GFXcanvas16 &c, const VibModel &m) {
  const Pill p = statusPill(m);
  const int textW = fontWidth(c, kBold, p.text);
  const int pad = 12, dotW = p.dot ? 14 : 0;
  const int pw = pad + dotW + textW + pad;
  c.fillRoundRect(6, 5, pw, 24, 12, p.fill);
  if (p.dot && !(p.blink && (m.nowMs / 500) % 2)) c.fillCircle(6 + pad + 4, 17, 5, p.color);
  fontText(c, kBold, 6 + pad + dotW, 23, p.color, p.text);
  if (m.mode == RecorderCore::Mode::Recording && m.sd.state == MeasStatus::State::Recording) {
    // Progress to the maximum duration, under the pill.
    const float frac = fminf(float(m.sd.recordingMs) / float(m.range.recordS * 1000), 1.0f);
    c.fillRect(6 + 8, 31, pw - 16, 2, kGrid);
    c.fillRect(6 + 8, 31, int(lroundf(frac * (pw - 16))), 2, kRed);
  }

  int left = W;
  drawBattery(c, W - 4, m, left);
  drawCard(c, left - 14, m);
}

// ---- Chart ----------------------------------------------------------------------------

// Message over the traces. Does not fit the plot on one line: two lines (break at the
// space closest to the middle), and the smaller font if that is still too wide.
void notice(GFXcanvas16 &c, uint16_t color, const char *s) {
  char line[2][40] = {"", ""};
  std::snprintf(line[0], sizeof line[0], "%s", s);
  const int maxW = kPlotW - 24;
  const GFXfont *font = kBold;
  if (fontWidth(c, font, s) > maxW) {
    const int len = int(std::strlen(s));
    int cut = -1;
    for (int i = 0; i < len; ++i)
      if (s[i] == ' ' && (cut < 0 || std::abs(i - len / 2) < std::abs(cut - len / 2))) cut = i;
    if (cut > 0) {
      std::snprintf(line[0], sizeof line[0], "%.*s", cut, s);
      std::snprintf(line[1], sizeof line[1], "%s", s + cut + 1);
    }
    if (std::max(fontWidth(c, font, line[0]), fontWidth(c, font, line[1])) > maxW) font = kSmall;
  }
  const bool two = line[1][0] != 0;
  int w = 0;
  for (const auto &l : line) w = std::max(w, fontWidth(c, font, l));
  w = std::min(w + 20, kPlotW - 4);
  const int h = two ? 48 : 30;
  const int x = kPlotX + (kPlotW - w) / 2, y = kPlotY + (kPlotH - h) / 2;
  c.fillRoundRect(x, y, w, h, 8, kPanel);
  c.drawRoundRect(x, y, w, h, 8, color);
  for (int i = 0; i < 2; ++i) {
    if (!line[i][0]) continue;
    const int baseline = two ? y + 20 + i * 20 : y + 20;
    fontText(c, font, x + (w - fontWidth(c, font, line[i])) / 2, baseline, color, line[i]);
  }
}

void dottedHLine(GFXcanvas16 &c, int x, int y, int w, uint16_t color, int step) {
  for (int i = 0; i < w; i += step) c.drawPixel(x + i, y, color);
}

void dashedHLine(GFXcanvas16 &c, int x, int y, int w, uint16_t color) {
  for (int i = 0; i < w; i += 8) c.drawFastHLine(x + i, y, i + 5 <= w ? 5 : w - i, color);
}

void drawChart(GFXcanvas16 &c, const VibModel &m) {
  const bool recording = m.mode == RecorderCore::Mode::Recording;
  c.fillRoundRect(kPanelX, kPanelY, kPanelW, kPanelH, 8, kPanel);
  c.drawRoundRect(kPanelX, kPanelY, kPanelW, kPanelH, 8, recording ? kRed : kPanelEdge);
  if (recording) c.drawRoundRect(kPanelX + 1, kPanelY + 1, kPanelW - 2, kPanelH - 2, 7, kRed);

  float peak = 0;  // Scale: the smallest step that holds everything on the screen.
  if (m.history)
    for (std::size_t i = 0; i < m.history->size(); ++i) {
      const SignalBin b = m.history->at(i);
      if (!b.valid) continue;
      for (int ch = 0; ch < 3; ++ch)
        peak = fmaxf(peak, fmaxf(fabsf(component(b.low, ch)), fabsf(component(b.high, ch))));
    }
  const float range = rangeui::chartRangeMs2(peak);
  const auto yPixel = [&](float v) {
    v = fmaxf(-range, fminf(range, v));
    return kPlotY + int(lroundf((range - v) * (kPlotH - 1) / (2 * range)));
  };
  const auto xPixel = [&](int column) { return kPlotX + 1 + column * (kPlotW - 3) / (vib::kBins - 1); };

  // Grid and zero line.
  for (int n = 0; n <= 4; ++n) {
    const int y = kPlotY + n * (kPlotH - 1) / 4;
    if (n != 2) dottedHLine(c, kPlotX, y, kPlotW, kGrid, 3);
  }
  for (int n = 1; n < 5; ++n) {
    const int x = kPlotX + n * (kPlotW - 1) / 5;  // One line per ~1.1 s.
    for (int y = kPlotY; y < kPlotY + kPlotH; y += 3) c.drawPixel(x, y, kGrid);
  }
  c.drawFastHLine(kPlotX, yPixel(0), kPlotW, kZero);

  // Scale, right-aligned in the left margin.
  char label[8];
  std::snprintf(label, sizeof label, "+%.0f", range);
  small(c, kPlotX - 4 - smallWidth(label), kPlotY, kMuted, label);
  small(c, kPlotX - 10, yPixel(0) - 3, kMuted, "0");
  std::snprintf(label, sizeof label, "-%.0f", range);
  small(c, kPlotX - 4 - smallWidth(label), kPlotY + kPlotH - 8, kMuted, label);
  small(c, kPlotX - 26, kPlotY + kPlotH / 4 - 3, kDim, "m/s2");

  // Traces: min/max bar per bin, joined last-to-first, 2 px wide.
  if (m.history) {
    const SignalHistory<vib::kBins> &hist = *m.history;
    const int first = vib::kBins - int(hist.size());  // Newest bin on the right edge.
    for (int ch = 0; ch < 3; ++ch) {
      bool previous = false;
      int prevX = 0, prevY = 0, gap = 0;
      const uint16_t color = kAxis[ch];
      for (std::size_t i = 0; i < hist.size(); ++i) {
        const SignalBin b = hist.at(i);
        if (!b.valid) {
          if (++gap > 3) previous = false;  // Bridge short gaps, break on real ones.
          continue;
        }
        gap = 0;
        const int x = xPixel(first + int(i));
        const int hi = yPixel(component(b.high, ch)), lo = yPixel(component(b.low, ch));
        const int y0 = yPixel(component(b.first, ch));
        if (previous) {
          c.drawLine(prevX, prevY, x, y0, color);
          c.drawLine(prevX, prevY + 1, x, y0 + 1, color);
        }
        c.fillRect(x, hi, 2, lo - hi + 2, color);
        prevX = x;
        prevY = yPixel(component(b.last, ch));
        previous = true;
      }
    }
  }

  // Bottom row: legend, then the live level against the threshold.
  int x = kPlotX;
  for (int i = 0; i < 3; ++i) {
    c.fillRoundRect(x, kRowY + 1, 8, 8, 2, kAxis[i]);
    const char axis[2] = {kAxisName[i], 0};
    small(c, x + 11, kRowY + 1, kMuted, axis);
    x += 22;
  }
  char lv[12];
  std::snprintf(lv, sizeof lv, m.sensorLive ? "%.2f" : "-", m.level);
  small(c, kPlotX + kPlotW - smallWidth(lv), kRowY + 1, kText, lv);

  // Over the traces: what keeps the recorder from working.
  if (!m.sensorLive) notice(c, kAmber, "BRAK CZUJNIKA");
  else if (m.hostOwns || m.mode == RecorderCore::Mode::Usb) notice(c, kBlue, "USB: ZAPIS WSTRZYMANY");
  else if (m.sd.state == MeasStatus::State::NoCard) notice(c, kAmber, "WLOZ KARTE I ZRESTARTUJ");
  else if (m.sd.state == MeasStatus::State::Error) {
    char s[40];
    std::snprintf(s, sizeof s, "%.32s", m.sd.message);
    notice(c, kRed, s);
  }
}

// ---- Statistics of the last measurement ------------------------------------------------

// At most 4 characters, as many decimals as fit ("0.25", "12.3", "-9.8", "123").
void fmt4(char *out, std::size_t size, float v) {
  std::snprintf(out, size, "%.2f", v);
  if (std::strlen(out) > 4) std::snprintf(out, size, "%.1f", v);
  if (std::strlen(out) > 4) std::snprintf(out, size, "%.0f", v);
}

// Live spectrum, the three axes overlaid in their colours (Z drawn last), scaled to the largest
// column; a marker on the strongest line of the XYZ vector.
void drawLiveSpectrum(GFXcanvas16 &c, const LiveSpectrum &live, int x, int y, int w, int h) {
  c.drawFastHLine(x, y + h - 1, w, kZero);
  const float top = live.top();
  constexpr int cols = LiveSpectrum::kCols;
  for (int a = 0; a < 3; ++a) {
    int prevY = -1;
    for (int px = 0; px < w; ++px) {
      const int c0 = px * cols / w, c1 = std::max(c0 + 1, (px + 1) * cols / w);
      float v = 0;
      for (int k = c0; k < c1 && k < cols; ++k) v = fmaxf(v, live.column(a, k));
      const int py = y + h - 1 - int(lroundf(fminf(v / top, 1.0f) * (h - 2)));
      if (prevY >= 0) c.drawLine(x + px - 1, prevY, x + px, py, kAxis[a]);
      prevY = py;
    }
  }
  if (live.domHz() > 0) {
    const int px = int(lroundf(live.domHz() / live.nyquistHz() * (w - 1)));
    c.fillTriangle(x + px - 2, y, x + px + 2, y, x + px, y + 3, kAmber);
  }
}

void drawStats(GFXcanvas16 &c, const VibModel &m) {
  const bool recording = m.mode == RecorderCore::Mode::Recording;
  c.fillRoundRect(kStatX, kPanelY, kStatW, kPanelH, 8, kPanel);
  c.drawRoundRect(kStatX, kPanelY, kStatW, kPanelH, 8, kPanelEdge);
  const int x0 = kStatX + 4, right = kStatX + kStatW - 5;
  const MeasStats *st = m.last && m.last->valid ? m.last : nullptr;

  // Top: the last measurement (title and table), or that there is none yet.
  char s[24];
  constexpr int labelW = 21, pitch = 27;
  const int hy = kPanelY + 19;
  int y = hy + 14 + 4 * 10;  // Below the table.
  if (!st) {
    small(c, x0, kPanelY + 6, kMuted, "OSTATNI POMIAR");
    const char *l1 = recording ? "NAGRYWAM" : "BRAK", *l2 = recording ? "..." : "POMIARU";
    fontText(c, kBold, kStatX + (kStatW - fontWidth(c, kBold, l1)) / 2, kPanelY + 44, recording ? kRed : kMuted, l1);
    fontText(c, kBold, kStatX + (kStatW - fontWidth(c, kBold, l2)) / 2, kPanelY + 64, recording ? kRed : kMuted, l2);
  } else {
    std::snprintf(s, sizeof s, "M%04u", m.lastNumber % 10000);
    small(c, x0, kPanelY + 6, recording ? kDim : kText, s);
    std::snprintf(s, sizeof s, "%.1f s", st->durationS);
    small(c, right - smallWidth(s), kPanelY + 6, recording ? kDim : kMuted, s);
    // Table: a column per axis, a row per statistic.
    for (int a = 0; a < 3; ++a) {
      const char axis[2] = {kAxisName[a], 0};
      small(c, x0 + labelW + a * pitch + 24 - 6 - 6, hy, kAxis[a], axis);
    }
    c.drawFastHLine(x0, hy + 10, kStatW - 10, kPanelEdge);
    struct Row { const char *label; float AxisStats::*field; };
    const Row rows[] = {{"sr", &AxisStats::mean}, {"RMS", &AxisStats::rms}, {"pik", &AxisStats::peak}, {"p-p", &AxisStats::p2p}};
    int ry = hy + 14;
    for (const Row &r : rows) {
      small(c, x0, ry, kMuted, r.label);
      for (int a = 0; a < 3; ++a) {
        char v[12];
        fmt4(v, sizeof v, st->axis[a].*(r.field));
        small(c, x0 + labelW + a * pitch + 24 - smallWidth(v), ry, kText, v);
      }
      ry += 10;
    }
  }
  c.drawFastHLine(x0, y + 1, kStatW - 10, kPanelEdge);

  // Bottom: the spectrum of the live signal (the measurement's own spectrum is on page 3).
  y += 5;
  const LiveSpectrum *live = m.live && m.live->valid() && m.sensorLive ? m.live : nullptr;
  small(c, x0, y, kMuted, "FFT");
  small(c, x0 + 21, y, kGreen, "NA ZYWO");
  if (!live) {
    small(c, x0, y + 30, kDim, "zbiera probki...");
    return;
  }
  if (live->domHz() > 0) {
    std::snprintf(s, sizeof s, "%.0fHz", live->domHz());
    small(c, right - smallWidth(s), y, kAmber, s);
  }
  drawLiveSpectrum(c, *live, x0, y + 12, kStatW - 10, kPanelY + kPanelH - 15 - (y + 12));
  small(c, x0, kPanelY + kPanelH - 12, kDim, "0");
  std::snprintf(s, sizeof s, "%.0f Hz", live->nyquistHz());
  small(c, right - smallWidth(s), kPanelY + kPanelH - 12, kDim, s);
}

// ---- Range finder ------------------------------------------------------------------------

void drawButton(GFXcanvas16 &c, const Rect &r, bool pressed, bool plus) {
  const int x = r.x + 4, y = r.y + 2, w = r.w - 8, h = r.h - 4;
  c.fillRoundRect(x, y, w, h, 10, pressed ? kButtonPressed : kButton);
  c.drawRoundRect(x, y, w, h, 10, pressed ? kAmber : kPanelEdge);
  const int cx = x + w / 2, cy = y + h / 2;
  c.fillRoundRect(cx - 11, cy - 2, 22, 5, 2, kText);
  if (plus) c.fillRoundRect(cx - 2, cy - 11, 5, 22, 2, kText);
}

// Distance now, a bar with the trigger distance in the middle (0 .. 2x the trigger),
// and what the range finder is doing. Bottom of pages 1 and 4.
void drawRangeStrip(GFXcanvas16 &c, const VibModel &m) {
  constexpr int y = 198, h = 36, barX = 132, barW = 176;
  c.fillRoundRect(kPanelX, y, 312, h, 8, kPanel);
  c.drawRoundRect(kPanelX, y, 312, h, 8, kPanelEdge);
  const bool missing = m.rangeState == VibModel::RangeState::Missing;
  const bool have = !missing && m.rangeState == VibModel::RangeState::Ranging && m.rangeMm != RangeTrigger::kNoReading;
  small(c, 12, y + 4, kMuted, "DALMIERZ");
  char value[12];
  if (have) std::snprintf(value, sizeof value, "%u", unsigned(m.rangeMm));
  else std::snprintf(value, sizeof value, "-");
  const bool near = have && m.rangeMm < m.range.distanceMm;
  const int after = fontText(c, kBold, 12, y + 31, near ? kRed : kText, value);
  fontText(c, kSmall, after + 3, y + 31, kMuted, "mm");

  const int by = y + 6, bh = 10;
  c.fillRoundRect(barX, by, barW, bh, 3, kGrid);
  if (have) {
    const float frac = fminf(float(m.rangeMm) / float(2 * m.range.distanceMm), 1.0f);
    const int fill = int(lroundf(frac * barW));
    if (fill > 0) c.fillRoundRect(barX, by, fill < 4 ? 4 : fill, bh, 3, near ? kRed : kGreen);
  }
  c.fillRect(barX + barW / 2 - 1, by - 2, 2, bh + 4, kAmber);
  char thr[24];
  std::snprintf(thr, sizeof thr, "PROG %d mm", m.range.distanceMm);
  small(c, barX, y + 23, kMuted, thr);
  const char *state = "CZEKAM";
  uint16_t color = kMuted;
  if (missing) {
    state = "BRAK DALMIERZA";
    color = kAmber;
  } else if (m.mode == RecorderCore::Mode::Recording) {
    state = "NAGRYWAM";
    color = kRed;
  } else if (m.mode == RecorderCore::Mode::Usb || m.hostOwns) {
    state = "USB";
    color = kBlue;
  } else if (m.rangeState == VibModel::RangeState::Ranging && !have) {
    state = "BRAK ECHA";
  }
  small(c, barX + barW - smallWidth(state), y + 23, color, state);
}

// Touch calibration: a cross on the target, the targets done so far dimmed.
void drawCalibration(GFXcanvas16 &c, const VibModel &m) {
  const touchcal::Point &t = touchcal::kTarget[m.calibStep];
  for (int i = 0; i < m.calibStep; ++i) {
    const touchcal::Point &d = touchcal::kTarget[i];
    c.drawCircle(int(d.x), int(d.y), 6, kDim);
  }
  const int x = int(t.x), y = int(t.y);
  c.drawFastHLine(x - 18, y, 37, kAmber);
  c.drawFastVLine(x, y - 18, 37, kAmber);
  c.drawCircle(x, y, 9, kAmber);
  c.fillCircle(x, y, 2, kText);
  char s[40];
  std::snprintf(s, sizeof s, "KALIBRACJA DOTYKU %d/%d", m.calibStep + 1, int(touchcal::kTargets));
  const int ty = t.y < 120 ? 150 : 74;
  fontText(c, kBold, W / 2 - fontWidth(c, kBold, s) / 2, ty, kText, s);
  const char *hint = "Dotknij srodka krzyza";
  fontText(c, kSmall, W / 2 - fontWidth(c, kSmall, hint) / 2, ty + 24, kMuted, hint);
}

// A wide button of the last page.
void drawWideButton(GFXcanvas16 &c, const Rect &r, const char *label, uint16_t text, uint16_t edge, bool pressed) {
  c.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? kButtonPressed : kButton);
  c.drawRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? kAmber : edge);
  fontText(c, kBold, r.x + (r.w - fontWidth(c, kBold, label)) / 2, r.y + r.h / 2 + 6, text, label);
}

// Page 5: USB disk mode (default: the recorder keeps the card), touch calibration and clearing
// the card of old measurements.
void drawUsbPage(GFXcanvas16 &c, const VibModel &m) {
  const bool recording = m.mode == RecorderCore::Mode::Recording;
  using B = usbui::Button;
  const bool usbOk = m.usbDisk || (m.usbHost && !recording);
  drawWideButton(c, usbui::kUsbButton, m.usbDisk ? "ZAKONCZ TRYB DYSKU" : "WLACZ TRYB DYSKU USB",
                 usbOk ? kText : kDim, m.usbDisk && usbOk ? kBlue : kPanelEdge, m.usbButton == B::Usb && usbOk);

  const char *line1, *line2;
  uint16_t color = kMuted;
  if (m.usbDisk) {
    line1 = "Karta jest w komputerze. Rejestracja wstrzymana.";
    line2 = "";
    color = kBlue;
  } else if (!m.usbHost) {
    line1 = "Kabel USB niepodlaczony.";
    line2 = "Port szeregowy dziala po podlaczeniu.";
  } else if (recording) {
    line1 = "Trwa nagrywanie. Poczekaj na koniec pomiaru.";
    line2 = "";
    color = kAmber;
  } else {
    line1 = "Rejestrator trzyma karte,";
    line2 = "komputer widzi port szeregowy.";
  }
  small(c, W / 2 - smallWidth(line1) / 2, 90, color, line1);
  small(c, W / 2 - smallWidth(line2) / 2, 101, color, line2);

  drawWideButton(c, usbui::kCalButton, "KALIBRACJA DOTYKU", kText, kPanelEdge, m.usbButton == B::Calibrate);

  // Clearing: only when idle (not recording, not handed to the computer, card ready, something to remove).
  const bool cleaning = m.sd.state == MeasStatus::State::Cleaning;
  const bool cleanOk = !recording && !m.hostOwns && m.mode != RecorderCore::Mode::Usb &&
                       m.sd.state == MeasStatus::State::Idle && m.sd.measurements > 0;
  char label[40];
  uint16_t text = cleanOk ? kText : kDim, edge = kPanelEdge;
  if (cleaning) {
    std::snprintf(label, sizeof label, "CZYSZCZENIE KARTY...");
    text = kAmber;
  } else if (m.cleanConfirm && cleanOk) {
    std::snprintf(label, sizeof label, "POTWIERDZ: USUN (%lu)", (unsigned long)m.sd.measurements);
    text = kText;
    edge = kRed;
  } else {
    std::snprintf(label, sizeof label, "WYCZYSC POMIARY (%lu)", (unsigned long)m.sd.measurements);
  }
  drawWideButton(c, usbui::kCleanButton, label, text, edge, m.usbButton == B::Clean && cleanOk);
  char note[48] = "";
  uint16_t noteColor = kMuted;
  if (m.cleanConfirm && cleanOk) {
    std::snprintf(note, sizeof note, "Dotknij ponownie w ciagu 5 s. Usuwa M*.CSV.");
    noteColor = kRed;
  } else if (m.cleanResult >= 0) {
    std::snprintf(note, sizeof note, "Usunieto %d plikow z karty.", m.cleanResult);
    noteColor = kGreen;
  } else if (!cleanOk && !cleaning) {
    std::snprintf(note, sizeof note, m.sd.measurements == 0 ? "Brak pomiarow na karcie." : "Teraz niedostepne.");
  }
  small(c, W / 2 - smallWidth(note) / 2, 210, noteColor, note);
}

// Page 4: the four settings, one row each.
void drawRangePage(GFXcanvas16 &c, const VibModel &m) {
  const struct { const char *label; } rows[rangeui::kParams] = {
      {"ODLEGLOSC WYZWALANIA"}, {"CZAS REJESTRACJI"}, {"PRE-ROLL"}, {"TRYB POMIARU"}};
  for (int i = 0; i < rangeui::kParams; ++i) {
    const rangeui::Param p = rangeui::Param(i);
    const bool minus = m.rangePress.dir < 0 && m.rangePress.param == p;
    const bool plus = m.rangePress.dir > 0 && m.rangePress.param == p;
    drawButton(c, rangeui::minusRect(p), minus, false);
    drawButton(c, rangeui::plusRect(p), plus, true);
    const int top = rangeui::rowRect(p).y, mid = W / 2;
    small(c, mid - smallWidth(rows[i].label) / 2, top + 3, kMuted, rows[i].label);
    char value[24];
    switch (p) {
    case rangeui::Param::Distance: std::snprintf(value, sizeof value, "%d mm", m.range.distanceMm); break;
    case rangeui::Param::Record: std::snprintf(value, sizeof value, "%d s", m.range.recordS); break;
    case rangeui::Param::PreRoll: std::snprintf(value, sizeof value, "%d ms", m.range.preRollMs); break;
    default:
      std::snprintf(value, sizeof value, "%s %lu ms", rangeui::modeName(m.range.mode),
                    (unsigned long)(rangeui::budgetUs(m.range.mode) / 1000));
      break;
    }
    fontText(c, kBold, mid - fontWidth(c, kBold, value) / 2, top + 30, kAmber, value);
  }
}

// ---- Peaks of the session (page 2) ----------------------------------------------------

void drawPageDots(GFXcanvas16 &c, int page) {
  for (int i = 0; i < vib::kPages; ++i)
    c.fillCircle(W / 2 + (2 * i - (vib::kPages - 1)) * 7, 236, 2, i == page ? kText : kDim);
}

void drawPeaks(GFXcanvas16 &c, const VibModel &m) {
  constexpr int panelY = 36, panelH = 196;
  constexpr int px = 44, py = 62, pw = 262, ph = 132;
  c.fillRoundRect(kPanelX, panelY, 312, panelH, 8, kPanel);
  c.drawRoundRect(kPanelX, panelY, 312, panelH, 8, kPanelEdge);

  static const VibPeaks none;
  const VibPeaks &log = m.peaks ? *m.peaks : none;
  const int n = int(log.size());
  const float range = peakchart::yRange(log.maxPeak());
  const auto yOf = [&](float v) { return py + ph - 1 - int(lroundf(fminf(v, range) / range * (ph - 1))); };

  small(c, kPanelX + 10, panelY + 8, kMuted, "MAKSIMA NAGRAN W SESJI [m/s2]");
  char s[24];
  std::snprintf(s, sizeof s, "razem: %d", int(log.firstIndex() - 1) + n);
  small(c, kPanelX + 312 - 10 - smallWidth(s), panelY + 8, kText, s);

  // Y axis: grid every quarter, labels at 0, half and full scale.
  for (int q = 0; q <= 4; ++q) {
    const int y = py + (ph - 1) - q * (ph - 1) / 4;
    if (q == 0) c.drawFastHLine(px, y, pw, kZero);
    else dottedHLine(c, px, y, pw, kGrid, 3);
    if (q % 2 == 0) {
      char v[12];
      fmt4(v, sizeof v, range * q / 4);
      small(c, px - 5 - smallWidth(v), y - 4, kMuted, v);
    }
  }

  if (n == 0) {
    const char *l1 = "BRAK NAGRAN";
    fontText(c, kBold, W / 2 - fontWidth(c, kBold, l1) / 2, py + ph / 2 - 2, kMuted, l1);
    const char *l2 = "maksima pojawia sie po pierwszym pomiarze";
    small(c, W / 2 - smallWidth(l2) / 2, py + ph / 2 + 8, kDim, l2);
    return;
  }

  // Bars: the plot width is shared by all recordings, so it fills up as they come.
  const float slot = float(pw) / n;
  const int bw = std::max(1, std::min(28, int(slot * 0.7f)));
  const bool dots = slot >= 14;
  const unsigned step = peakchart::labelStep(std::size_t(n), 8);
  for (int i = 0; i < n; ++i) {
    const PeakEntry &e = log.at(std::size_t(i));
    const int cx = px + int((i + 0.5f) * slot);
    const int top = yOf(e.peak);
    c.fillRect(cx - bw / 2, top, bw, py + ph - 1 - top, i == n - 1 ? kAmber : kBlue);
    if (dots)
      for (int a = 0; a < 3; ++a) {
        const int y = yOf(e.axis[a]);
        c.fillRect(cx - 2 + (a - 1) * 5, y - 2, 5, 5, kBg);  // Outline: a dot on a bar of its own colour.
        c.fillRect(cx - 1 + (a - 1) * 5, y - 1, 3, 3, kAxis[a]);
      }
    const unsigned index = log.firstIndex() + unsigned(i);
    if (index % step == 0) {
      std::snprintf(s, sizeof s, "%u", index);
      small(c, cx - smallWidth(s) / 2, py + ph + 4, i == n - 1 ? kAmber : kMuted, s);
    }
  }

  // Legend and the latest measurement.
  const int ly = panelY + panelH - 18;
  c.fillRect(kPanelX + 10, ly + 1, 8, 8, kBlue);
  small(c, kPanelX + 22, ly + 1, kMuted, "wektor");
  int x = kPanelX + 22 + smallWidth("wektor") + 12;
  for (int a = 0; a < 3; ++a) {
    c.fillRect(x, ly + 3, 4, 4, kAxis[a]);
    const char axis[2] = {kAxisName[a], 0};
    small(c, x + 7, ly + 1, kMuted, axis);
    x += 24;
  }
  const PeakEntry &last = log.at(std::size_t(n - 1));
  std::snprintf(s, sizeof s, "M%04u", last.number % 10000);
  const int right = kPanelX + 312 - 10;
  char v[12];
  std::snprintf(v, sizeof v, "%.2f", last.peak);
  small(c, right - smallWidth(v), ly + 1, kAmber, v);
  small(c, right - smallWidth(v) - 6 - smallWidth(s), ly + 1, kMuted, s);
}

// ---- FFT (page 3) ---------------------------------------------------------------------

// Three spectra one below the other (X, Y, Z of the sensor) on a common amplitude
// scale, so the axes can be compared; on the right the three strongest lines of each.
void drawFftPage(GFXcanvas16 &c, const VibModel &m) {
  constexpr int panelY = 36, panelH = 196;
  constexpr int plotW = 200, tableX = 208, tableW = 108;
  const MeasStats *st = m.last && m.last->valid ? m.last : nullptr;
  const bool ok = st && st->hasSpectrum;
  c.fillRoundRect(kPanelX, panelY, plotW, panelH, 8, kPanel);
  c.drawRoundRect(kPanelX, panelY, plotW, panelH, 8, kPanelEdge);
  c.fillRoundRect(tableX, panelY, tableW, panelH, 8, kPanel);
  c.drawRoundRect(tableX, panelY, tableW, panelH, 8, kPanelEdge);
  small(c, kPanelX + 10, panelY + 8, kMuted, "WIDMO FFT [m/s2]");
  small(c, tableX + 8, panelY + 8, kMuted, "SKLADOWE");

  if (!ok) {
    const char *l1 = !st ? "BRAK POMIARU" : "ZA KROTKI POMIAR";
    const char *l2 = !st ? "widmo pojawi sie po pomiarze" : "FFT potrzebuje 1024 probek";
    fontText(c, kBold, kPanelX + (plotW - fontWidth(c, kBold, l1)) / 2, panelY + 96, kMuted, l1);
    small(c, kPanelX + (plotW - smallWidth(l2)) / 2, panelY + 106, kDim, l2);
    return;
  }

  char s[24];
  std::snprintf(s, sizeof s, "M%04u", m.lastNumber % 10000);
  small(c, kPanelX + plotW - 10 - smallWidth(s), panelY + 8, kText, s);

  // Plots: frequency 0..Nyquist, amplitude 0..top (the same top for the three).
  constexpr int px = 34, pw = 160, ph = 42, py0 = 58, pitch = 48;
  constexpr int bins = int(MeasStats::kFftSize / 2) + 1;
  float maxAmp = 0;
  for (int a = 0; a < 3; ++a)
    for (int k = 2; k < bins; ++k) maxAmp = fmaxf(maxAmp, st->axisFft[a][k]);  // No DC leakage.
  const float top = peakchart::yRange(maxAmp, 0.05f);
  const float nyq = st->nyquistHz;
  for (int a = 0; a < 3; ++a) {
    const int py = py0 + a * pitch;
    const auto yOf = [&](float v) { return py + ph - 1 - int(lroundf(fminf(v, top) / top * (ph - 1))); };
    c.drawFastHLine(px, py + ph - 1, pw, kZero);
    dottedHLine(c, px, py + (ph - 1) / 2, pw, kGrid, 3);
    const char axis[2] = {kAxisName[a], 0};
    fontText(c, kBold, kPanelX + 8, py + ph / 2 + 6, kAxis[a], axis);
    if (a == 0) {  // Scale of the common amplitude axis.
      char v[12];
      fmt4(v, sizeof v, top);
      small(c, kPanelX + 4 + 12, py - 1, kMuted, v);
    }
    for (int x = 0; x < pw; ++x) {  // Highest bin of each pixel column.
      // Bins [k0, k1), no overlap with the next column; the last one takes Nyquist too.
      const int k0 = x * (bins - 1) / pw;
      const int k1 = x == pw - 1 ? bins : std::max(k0 + 1, (x + 1) * (bins - 1) / pw);
      float v = 0;
      for (int k = std::max(k0, 1); k < k1 && k < bins; ++k) v = fmaxf(v, st->axisFft[a][k]);
      const int y = yOf(v);
      if (py + ph - 1 - y > 0) c.drawFastVLine(px + x, y, py + ph - 1 - y, kAxis[a]);
    }
    for (int i = 0; i < st->axisPeakCount[a]; ++i) {  // Numbered markers on the lines of the table.
      const SpectrumPeak &p = st->axisPeaks[a][i];
      const int x = px + int(lroundf(p.hz / nyq * (pw - 1))), y = yOf(p.amp);
      c.fillTriangle(x - 2, y - 5, x + 2, y - 5, x, y - 1, kAmber);
      char n[2] = {char('1' + i), 0};
      small(c, x - 2, std::max(y - 14, py - 4), kAmber, n);
    }
  }
  const int axisY = py0 + 3 * pitch - (pitch - ph);
  const float tick = peakchart::niceCeil(nyq / 4);
  for (float f = 0; f <= nyq; f += tick) {
    const int x = px + int(lroundf(f / nyq * (pw - 1)));
    if (f > 0)
      for (int a = 0; a < 3; ++a)
        for (int y = py0 + a * pitch; y < py0 + a * pitch + ph; y += 3) c.drawPixel(x, y, kGrid);
    std::snprintf(s, sizeof s, "%.0f", f);
    small(c, std::min(x - smallWidth(s) / 2, px + pw - smallWidth(s)), axisY + 4, kMuted, s);
  }
  small(c, kPanelX + plotW - 10 - 12, axisY + 4 + 10, kDim, "Hz");
  std::snprintf(s, sizeof s, "df %.2f Hz", st->binHz);
  small(c, kPanelX + 10, axisY + 4 + 10, kDim, s);

  // Table: per axis its three strongest lines: axis, rank, frequency, amplitude.
  const int x0 = tableX + 8;
  small(c, x0 + 24, panelY + 22, kDim, "Hz");
  small(c, tableX + tableW - 6 - smallWidth("m/s2"), panelY + 22, kDim, "m/s2");
  c.drawFastHLine(x0, panelY + 32, tableW - 16, kPanelEdge);
  for (int a = 0; a < 3; ++a) {
    for (int i = 0; i < int(MeasStats::kAxisPeaks); ++i) {
      const int base = panelY + 48 + (a * 3 + i) * 17 + a * 3;
      // Built-in 6x8 font, like the other small labels; `base` is the baseline of the row.
      const int top = base - 8;
      const char axis[2] = {kAxisName[a], 0};
      char n[2] = {char('1' + i), 0};
      small(c, x0, top, kAxis[a], axis);
      small(c, x0 + 7, top, i < st->axisPeakCount[a] ? kAmber : kDim, n);
      if (i >= st->axisPeakCount[a]) {
        small(c, x0 + 24, top, kDim, "-");
        continue;
      }
      const SpectrumPeak &p = st->axisPeaks[a][i];
      std::snprintf(s, sizeof s, p.hz < 9.95f ? "%.1f" : "%.0f", p.hz);  // Bins are ~0.5 Hz wide.
      small(c, x0 + 24, top, kText, s);
      char amp[12];
      fmt4(amp, sizeof amp, p.amp);
      small(c, tableX + tableW - 8 - smallWidth(amp), top, kMuted, amp);
    }
    if (a < 2) c.drawFastHLine(x0, panelY + 48 + (a * 3 + 2) * 17 + a * 3 + 4, tableW - 16, kGrid);
  }
}

} // namespace

void drawVibScreen(GFXcanvas16 &c, const VibModel &m) {
  c.fillScreen(kBg);
  c.setTextWrap(false);
  if (m.calibStep >= 0) {  // Whole screen for the targets (the first one is under the status bar).
    drawCalibration(c, m);
    c.setFont(nullptr);
    return;
  }
  drawStatusBar(c, m);
  if (m.page == 4) {
    drawUsbPage(c, m);
  } else if (m.page == 3) {
    drawRangePage(c, m);
    drawRangeStrip(c, m);
  } else if (m.page == 2) {
    drawFftPage(c, m);
  } else if (m.page == 1) {
    drawPeaks(c, m);
  } else {
    drawChart(c, m);
    drawStats(c, m);
    drawRangeStrip(c, m);
  }
  drawPageDots(c, m.page);
  c.setFont(nullptr);
}
