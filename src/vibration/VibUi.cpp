#include "VibUi.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

// Single screen of the vibration recorder, 320x240, laid out for this panel
// (proportional FreeSans fonts, not the scaled 6x8 font of the Feather screens):
//
//   y0    status bar: state pill (CZUWAM / REC 12.3 / 30 s + progress / USB / ...), card, battery
//   y36   left 2/3: chart panel, XYZ dynamic acceleration (5.4 s), threshold zones,
//         bottom row: axis legend, live level against the threshold
//         right 1/3: statistics panel of the last recorded measurement (mean, RMS,
//         peak, peak-to-peak per axis), its strongest frequency and spectrum
//   y192  [ - ]   PROG WYZWALANIA  2.0 m/s2   [ + ]   (thrui::kMinus / kPlus)
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
  const int x = mid - (vw + 6 + uw) / 2, baseline = thrui::kMinus.y + 38;
  const int after = fontText(c, kBig, x, baseline, kAmber, value);
  fontText(c, kSmall, after + 6, baseline, kMuted, unit);
}

} // namespace

void drawVibScreen(GFXcanvas16 &c, const VibModel &m) {
  c.fillScreen(kBg);
  c.setTextWrap(false);
  drawStatusBar(c, m);
  drawChart(c, m);
  drawStats(c, m);
  drawThreshold(c, m);
  c.setFont(nullptr);
}
