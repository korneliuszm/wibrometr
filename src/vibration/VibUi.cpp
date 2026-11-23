#include "VibUi.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// Three screens of the vibration recorder (swipe sideways), 320x240, laid out for this panel
// (proportional FreeSans fonts, not the scaled 6x8 font of the Feather screens):
//
//   y0    status bar: state pill (CZUWAM / REC 12.3 / 30 s + progress / USB / ...), card, battery
//   y36   left 2/3: chart panel, XYZ dynamic acceleration (5.4 s), threshold zones,
//         bottom row: axis legend, live level against the threshold
//         right 1/3: statistics panel of the last recorded measurement (mean, RMS,
//         peak, peak-to-peak per axis), its strongest frequency and spectrum
//   y192  [ - ]   PROG WYZWALANIA  2.0 m/s2   [ + ]   (thrui::kMinus / kPlus)
//
// Page 2 (peaks of the session): the status bar, then one panel with a bar per
// recorded measurement (its largest XYZ vector length, dots: largest per axis);
// both axes scale to what has been recorded since power-up.
//
// Page 3 (FFT): the status bar, the spectrum of the last measurement in the left 2/3
// (frequency axis to Nyquist, amplitude auto-scaled, numbered markers on the
// strongest lines) and a table of those lines in the right 1/3.
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
constexpr uint16_t kQuietBand = rgb(22, 30, 44);  // Between the threshold lines.
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
                  (unsigned long)(ms / 100 % 10), (unsigned long)(vib::kMaxRecordUs / 1000000));
  } else if (m.sd.state == MeasStatus::State::NoCard) {
    p = {rgb(84, 62, 10), kAmber, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BRAK KARTY");
  } else if (m.sd.state == MeasStatus::State::Error) {
    p = {rgb(96, 20, 26), rgb(255, 130, 130), false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BLAD KARTY");
  } else if (!m.sensorLive) {
    p = {rgb(84, 62, 10), kAmber, false, false, ""};
    std::snprintf(p.text, sizeof p.text, "BRAK CZUJNIKA");
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
    const float frac = fminf(float(m.sd.recordingMs) / (vib::kMaxRecordUs / 1000), 1.0f);
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

  const float range = thrui::chartRangeMs2(m.thresholdDeci);
  const float thr = m.thresholdDeci / 10.0f;
  const auto yPixel = [&](float v) {
    v = fmaxf(-range, fminf(range, v));
    return kPlotY + int(lroundf((range - v) * (kPlotH - 1) / (2 * range)));
  };
  const auto xPixel = [&](int column) { return kPlotX + 1 + column * (kPlotW - 3) / (vib::kBins - 1); };

  // Band below the threshold, grid, zero line, threshold lines.
  const int yThr = yPixel(thr), yThrNeg = yPixel(-thr);
  c.fillRect(kPlotX, yThr, kPlotW, yThrNeg - yThr + 1, kQuietBand);
  for (int n = 0; n <= 4; ++n) {
    const int y = kPlotY + n * (kPlotH - 1) / 4;
    if (n != 2) dottedHLine(c, kPlotX, y, kPlotW, kGrid, 3);
  }
  for (int n = 1; n < 5; ++n) {
    const int x = kPlotX + n * (kPlotW - 1) / 5;  // One line per ~1.1 s.
    for (int y = kPlotY; y < kPlotY + kPlotH; y += 3) c.drawPixel(x, y, kGrid);
  }
  c.drawFastHLine(kPlotX, yPixel(0), kPlotW, kZero);
  dashedHLine(c, kPlotX, yThr, kPlotW, kAmber);
  dashedHLine(c, kPlotX, yThrNeg, kPlotW, kAmber);

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
  const int barX = x + 4, barW = kPlotX + kPlotW - 32 - barX, barY = kRowY + 1, barH = 8;
  const float full = 2 * thr;  // Threshold in the middle of the bar.
  const float frac = fminf(m.level / full, 1.0f);
  const bool over = m.level >= thr;
  c.fillRoundRect(barX, barY, barW, barH, 3, kGrid);
  const int fill = int(lroundf(frac * barW));
  if (m.sensorLive && fill > 0) c.fillRoundRect(barX, barY, fill < 4 ? 4 : fill, barH, 3, over ? kRed : kGreen);
  c.fillRect(barX + barW / 2 - 1, barY - 2, 2, barH + 4, kAmber);
  char lv[12];
  std::snprintf(lv, sizeof lv, m.sensorLive ? "%.2f" : "-", m.level);
  small(c, kPlotX + kPlotW - smallWidth(lv), kRowY + 1, over ? kRed : kText, lv);

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

void drawSpectrum(GFXcanvas16 &c, const MeasStats &st, int x, int y, int w, int h) {
  const uint16_t kFft = rgb(190, 150, 255);
  c.drawFastHLine(x, y + h - 1, w, kZero);
  float top = 0.05f;  // Do not blow up the noise floor of a quiet measurement.
  for (float v : st.spectrum) top = fmaxf(top, v);
  constexpr int cols = int(MeasStats::kSpectrumCols);
  for (int px = 0; px < w; ++px) {
    const int c0 = px * cols / w, c1 = std::max(c0 + 1, (px + 1) * cols / w);
    float v = 0;
    for (int k = c0; k < c1 && k < cols; ++k) v = fmaxf(v, st.spectrum[k]);
    const int bar = int(lroundf(v / top * (h - 2)));
    if (bar > 0) c.drawFastVLine(x + px, y + h - 1 - bar, bar, kFft);
  }
  if (st.domHz > 0) {  // Marker of the strongest line.
    const int px = int(lroundf(st.domHz / st.nyquistHz * (w - 1)));
    c.fillTriangle(x + px - 2, y, x + px + 2, y, x + px, y + 3, kAmber);
  }
}

void drawStats(GFXcanvas16 &c, const VibModel &m) {
  const bool recording = m.mode == RecorderCore::Mode::Recording;
  c.fillRoundRect(kStatX, kPanelY, kStatW, kPanelH, 8, kPanel);
  c.drawRoundRect(kStatX, kPanelY, kStatW, kPanelH, 8, kPanelEdge);
  const int x0 = kStatX + 4, right = kStatX + kStatW - 5;
  const MeasStats *st = m.last && m.last->valid ? m.last : nullptr;

  // Title: which measurement, how long.
  char s[24];
  if (!st) {
    small(c, x0, kPanelY + 6, kMuted, "OSTATNI POMIAR");
    const char *l1 = recording ? "NAGRYWAM" : "BRAK", *l2 = recording ? "..." : "POMIARU";
    fontText(c, kBold, kStatX + (kStatW - fontWidth(c, kBold, l1)) / 2, kPanelY + 72, recording ? kRed : kMuted, l1);
    fontText(c, kBold, kStatX + (kStatW - fontWidth(c, kBold, l2)) / 2, kPanelY + 94, recording ? kRed : kMuted, l2);
    return;
  }
  std::snprintf(s, sizeof s, "M%04u", m.lastNumber % 10000);
  small(c, x0, kPanelY + 6, recording ? kDim : kText, s);
  std::snprintf(s, sizeof s, "%.1f s", st->durationS);
  small(c, right - smallWidth(s), kPanelY + 6, recording ? kDim : kMuted, s);

  // Table: a column per axis, a row per statistic.
  constexpr int labelW = 21, pitch = 27;
  const int hy = kPanelY + 19;
  for (int a = 0; a < 3; ++a) {
    const char axis[2] = {kAxisName[a], 0};
    small(c, x0 + labelW + a * pitch + 24 - 6 - 6, hy, kAxis[a], axis);
  }
  c.drawFastHLine(x0, hy + 10, kStatW - 10, kPanelEdge);
  struct Row { const char *label; float AxisStats::*field; };
  const Row rows[] = {{"sr", &AxisStats::mean}, {"RMS", &AxisStats::rms}, {"pik", &AxisStats::peak}, {"p-p", &AxisStats::p2p}};
  int y = hy + 14;
  for (const Row &r : rows) {
    small(c, x0, y, kMuted, r.label);
    for (int a = 0; a < 3; ++a) {
      char v[12];
      fmt4(v, sizeof v, st->axis[a].*(r.field));
      small(c, x0 + labelW + a * pitch + 24 - smallWidth(v), y, kText, v);
    }
    y += 10;
  }
  c.drawFastHLine(x0, y + 1, kStatW - 10, kPanelEdge);

  // Strongest frequency and the spectrum.
  y += 5;
  if (st->hasSpectrum) {
    char f[12];
    std::snprintf(f, sizeof f, "%.0f", st->domHz);
    small(c, x0, y, kMuted, "FFT");
    const int after = x0 + 24;
    small(c, after, y, kAmber, f);
    small(c, after + smallWidth(f) + 3, y, kMuted, "Hz");
    drawSpectrum(c, *st, x0, y + 13, kStatW - 10, kPanelY + kPanelH - 16 - (y + 13));
    small(c, x0, kPanelY + kPanelH - 12, kDim, "0");
    std::snprintf(s, sizeof s, "%.0f Hz", st->nyquistHz);
    small(c, right - smallWidth(s), kPanelY + kPanelH - 12, kDim, s);
  } else {
    small(c, x0, y, kMuted, "FFT: za krotki");
  }
}

// ---- Threshold -------------------------------------------------------------------------

void drawButton(GFXcanvas16 &c, const Rect &r, bool pressed, bool plus) {
  const int x = r.x + 4, y = r.y + 2, w = r.w - 8, h = r.h - 4;
  c.fillRoundRect(x, y, w, h, 10, pressed ? kButtonPressed : kButton);
  c.drawRoundRect(x, y, w, h, 10, pressed ? kAmber : kPanelEdge);
  const int cx = x + w / 2, cy = y + h / 2;
  c.fillRoundRect(cx - 11, cy - 2, 22, 5, 2, kText);
  if (plus) c.fillRoundRect(cx - 2, cy - 11, 5, 22, 2, kText);
}

void drawThreshold(GFXcanvas16 &c, const VibModel &m) {
  drawButton(c, thrui::kMinus, m.pressed == thrui::Button::Minus, false);
  drawButton(c, thrui::kPlus, m.pressed == thrui::Button::Plus, true);
  const int left = thrui::kMinus.x + thrui::kMinus.w, right = thrui::kPlus.x, mid = (left + right) / 2;
  const char *label = "PROG WYZWALANIA";
  small(c, mid - smallWidth(label) / 2, thrui::kMinus.y + 2, kMuted, label);
  char value[12];
  std::snprintf(value, sizeof value, "%.1f", m.thresholdDeci / 10.0f);
  const char *unit = "m/s2";
  const int vw = fontWidth(c, kBig, value), uw = fontWidth(c, kSmall, unit);
  const int x = mid - (vw + 6 + uw) / 2, baseline = thrui::kMinus.y + 36;
  const int after = fontText(c, kBig, x, baseline, kAmber, value);
  fontText(c, kSmall, after + 6, baseline, kMuted, unit);
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

void drawFftPage(GFXcanvas16 &c, const VibModel &m) {
  constexpr int panelY = 36, panelH = 196;
  constexpr int plotW = 200, tableX = 208, tableW = 108;
  const uint16_t kFft = rgb(190, 150, 255);
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

  // Plot: frequency 0..Nyquist, amplitude 0..top.
  constexpr int px = 40, py = 60, pw = 152, ph = 136;
  constexpr int bins = int(MeasStats::kFftSize / 2) + 1;
  float maxAmp = 0;
  for (int k = 0; k < bins; ++k) maxAmp = fmaxf(maxAmp, k >= 2 ? st->fft[k] : 0);  // No DC leakage.
  const float top = peakchart::yRange(maxAmp, 0.05f);
  const auto yOf = [&](float v) { return py + ph - 1 - int(lroundf(fminf(v, top) / top * (ph - 1))); };
  for (int q = 0; q <= 4; ++q) {
    const int y = py + (ph - 1) - q * (ph - 1) / 4;
    if (q == 0) c.drawFastHLine(px, y, pw, kZero);
    else dottedHLine(c, px, y, pw, kGrid, 3);
    if (q % 2 == 0) {
      char v[12];
      fmt4(v, sizeof v, top * q / 4);
      small(c, px - 4 - smallWidth(v), y - 4, kMuted, v);
    }
  }
  const float nyq = st->nyquistHz;
  const float tick = peakchart::niceCeil(nyq / 6);
  for (float f = 0; f <= nyq; f += tick) {
    const int x = px + int(lroundf(f / nyq * (pw - 1)));
    if (f > 0) for (int y = py; y < py + ph; y += 3) c.drawPixel(x, y, kGrid);
    std::snprintf(s, sizeof s, "%.0f", f);
    small(c, std::min(x - smallWidth(s) / 2, px + pw - smallWidth(s)), py + ph + 4, kMuted, s);
  }
  small(c, kPanelX + plotW - 10 - 12, py + ph + 14, kDim, "Hz");

  for (int x = 0; x < pw; ++x) {  // Highest bin of each pixel column.
    // Bins [k0, k1), no overlap with the next column; the last one takes Nyquist too.
    const int k0 = x * (bins - 1) / pw;
    const int k1 = x == pw - 1 ? bins : std::max(k0 + 1, (x + 1) * (bins - 1) / pw);
    float v = 0;
    for (int k = std::max(k0, 1); k < k1 && k < bins; ++k) v = fmaxf(v, st->fft[k]);
    const int y = yOf(v);
    if (py + ph - 1 - y > 0) c.drawFastVLine(px + x, y, py + ph - 1 - y, kFft);
  }
  for (int i = 0; i < st->peakCount; ++i) {  // Numbered markers on the lines of the table.
    const SpectrumPeak &p = st->peaks[i];
    const int x = px + int(lroundf(p.hz / nyq * (pw - 1))), y = yOf(p.amp);
    c.fillTriangle(x - 2, y - 5, x + 2, y - 5, x, y - 1, kAmber);
    char n[2] = {char('1' + i), 0};
    small(c, x - 2, std::max(y - 15, py - 1), kAmber, n);
  }

  // Table: number, frequency, amplitude.
  const int x0 = tableX + 8;
  small(c, x0, panelY + 24, kDim, "#");
  small(c, x0 + 18, panelY + 24, kDim, "Hz");
  small(c, tableX + tableW - 8 - smallWidth("m/s2"), panelY + 24, kDim, "m/s2");
  c.drawFastHLine(x0, panelY + 34, tableW - 16, kPanelEdge);
  for (int i = 0; i < int(MeasStats::kPeaks); ++i) {
    const int base = panelY + 54 + i * 24;
    if (i >= st->peakCount) {
      fontText(c, kSmall, x0, base, kDim, "-");
      continue;
    }
    const SpectrumPeak &p = st->peaks[i];
    char n[2] = {char('1' + i), 0};
    fontText(c, kBold, x0, base, kAmber, n);
    std::snprintf(s, sizeof s, p.hz < 99.95f ? "%.1f" : "%.0f", p.hz);
    fontText(c, kSmall, x0 + 18, base, kText, s);
    char a[12];
    fmt4(a, sizeof a, p.amp);
    fontText(c, kSmall, tableX + tableW - 8 - fontWidth(c, kSmall, a), base, kMuted, a);
  }
  c.drawFastHLine(x0, panelY + panelH - 30, tableW - 16, kPanelEdge);
  std::snprintf(s, sizeof s, "df %.2f Hz", st->binHz);
  small(c, x0, panelY + panelH - 24, kDim, s);
  std::snprintf(s, sizeof s, "fp %.0f Hz", st->rateHz);
  small(c, x0, panelY + panelH - 13, kDim, s);
}

} // namespace

void drawVibScreen(GFXcanvas16 &c, const VibModel &m) {
  c.fillScreen(kBg);
  c.setTextWrap(false);
  drawStatusBar(c, m);
  if (m.page == 2) {
    drawFftPage(c, m);
  } else if (m.page == 1) {
    drawPeaks(c, m);
  } else {
    drawChart(c, m);
    drawStats(c, m);
    drawThreshold(c, m);
  }
  drawPageDots(c, m.page);
  c.setFont(nullptr);
}
