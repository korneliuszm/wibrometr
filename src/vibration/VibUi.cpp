#include "VibUi.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "UiTheme.h"
#include "ui/Draw.h"

// Single screen of the vibration recorder (320x240):
//   y0    header (state of the recorder, size 2)
//   y30   XYZ chart of the dynamic acceleration, thrui::kChart (26,30)-(319,175)
//   y180  "pomiarow: N   karta M MB" and the axis legend
//   y196  [ - ]   PROG 2.0 m/s2   [ + ]   (thrui::kMinus / thrui::kPlus)
// All text is Polish without diacritics (the GFX font has none).

namespace {

using ui::text;
using ui::textf;
constexpr Rect kChart = thrui::kChart;
constexpr int kHeaderY = 6, kInfoY = 180;

void centered(GFXcanvas16 &c, int x, int w, int y, int size, uint16_t color, const char *s) {
  const int width = int(std::strlen(s)) * 6 * size;
  text(c, x + (w - width) / 2, y, size, color, s);
}

// Centred notice over the chart, on a plain box so old traces do not hide it.
void notice(GFXcanvas16 &c, uint16_t color, const char *s) {
  const int width = int(std::strlen(s)) * 12 + 16;
  const int x = kChart.x + (kChart.w - width) / 2, y = kChart.y + kChart.h / 2 - 14;
  c.fillRect(x, y, width, 28, ui::bg);
  c.drawRect(x, y, width, 28, ui::edge);
  centered(c, x, width, y + 6, 2, color, s);
}

// Header, by priority:
//   1. USB (host owns the card, or the hand-over is still under way): nothing is
//      recorded, and the card is not the recorder's to report on.
//   2. NAGRYWAM: a measurement is being written.
//   3. Card problems (BRAK KARTY, BLAD): nothing could be recorded even with a sensor.
//   4. BRAK CZUJNIKA: card fine, no samples. A lost sensor is also shown in the
//      chart area whatever the header says (drawChart), so it is never hidden.
//   5. CZUWAM.
void drawHeader(GFXcanvas16 &c, const VibModel &m) {
  if (m.hostOwns || m.mode == RecorderCore::Mode::Usb || m.sd.state == MeasStatus::State::Parked) {
    text(c, 0, kHeaderY, 2, ui::blue, "USB: KARTA U KOMPUTERA");
  } else if (m.mode == RecorderCore::Mode::Recording) {
    c.fillCircle(7, kHeaderY + 7, 6, ui::red);
    textf(c, 18, kHeaderY, 2, ui::white, "NAGRYWAM %.1fs M%04u", m.sd.recordingMs / 1000.0f, m.sd.number);
  } else if (m.sd.state == MeasStatus::State::NoCard) {
    text(c, 0, kHeaderY, 2, ui::yellow, "BRAK KARTY - WLOZ I RESET");
  } else if (m.sd.state == MeasStatus::State::Error) {
    // 26 characters fit at size 2; "BLAD: " takes 6.
    textf(c, 0, kHeaderY, 2, ui::red, "BLAD: %.20s", m.sd.message);
  } else if (!m.sensorLive) {
    text(c, 0, kHeaderY, 2, ui::yellow, "BRAK CZUJNIKA");
  } else {
    text(c, 0, kHeaderY, 2, ui::green, "CZUWAM");
  }
  c.drawFastHLine(0, kHeaderY + 20, 320, ui::edge);
}

// Same drawing as drawWave in src/waveshare/ui/Screens.cpp (min/max bar per bin,
// joined from the last value of one bin to the first of the next), stretched to
// the chart width. Sensor axes as recorded in the files (no ui::shown sign).
void drawChart(GFXcanvas16 &c, const VibModel &m) {
  const float range = thrui::chartRangeMs2(m.thresholdDeci);
  const int top = kChart.y, h = kChart.h, left = kChart.x, w = kChart.w;
  const auto yPixel = [&](float v) {
    v = fmaxf(-range, fminf(range, v));
    return top + int(lroundf((range - v) * (h - 1) / (2 * range)));
  };
  const auto xPixel = [&](int column) { return left + 1 + column * (w - 2) / (vib::kBins - 1); };

  for (int n = 0; n <= 4; ++n) {
    c.drawFastHLine(left, top + n * (h - 1) / 4, w, ui::grid);
    c.drawFastVLine(left + n * (w - 1) / 4, top, h, ui::grid);
  }
  // Scale on the left, right-aligned in the 26 px margin.
  char label[8];
  std::snprintf(label, sizeof label, "+%.0f", range);
  text(c, left - 2 - int(std::strlen(label)) * 6, top, 1, ui::muted, label);
  text(c, left - 8, top + h / 2 - 4, 1, ui::muted, "0");
  std::snprintf(label, sizeof label, "-%.0f", range);
  text(c, left - 2 - int(std::strlen(label)) * 6, top + h - 8, 1, ui::muted, label);

  // Threshold lines. The trigger works on the magnitude of the vector, so an axis
  // past a line always means a crossing; a crossing may also show below the lines.
  const float thr = m.thresholdDeci / 10.0f;
  c.drawFastHLine(left, yPixel(thr), w, ui::yellow);
  c.drawFastHLine(left, yPixel(-thr), w, ui::yellow);

  if (m.history) {
    const SignalHistory<vib::kBins> &hist = *m.history;
    const int first = vib::kBins - int(hist.size());  // Newest bin on the right edge.
    for (int ch = 0; ch < 3; ++ch) {
      bool previous = false;
      int prevX = 0, prevY = 0;
      for (std::size_t i = 0; i < hist.size(); ++i) {
        const SignalBin b = hist.at(i);
        if (!b.valid) {
          previous = false;
          continue;
        }
        const int x = xPixel(first + int(i));
        const int hi = yPixel(ui::component(b.high, ch)), lo = yPixel(ui::component(b.low, ch));
        if (previous) c.drawLine(prevX, prevY, x, yPixel(ui::component(b.first, ch)), ui::kAxisColor[ch]);
        c.drawFastVLine(x, hi, lo - hi + 1, ui::kAxisColor[ch]);
        prevX = x;
        prevY = yPixel(ui::component(b.last, ch));
        previous = true;
      }
    }
  }

  if (!m.sensorLive) notice(c, ui::yellow, "BRAK CZUJNIKA");
  else if (m.hostOwns || m.mode == RecorderCore::Mode::Usb) notice(c, ui::muted, "ZAPIS WSTRZYMANY");
}

void drawInfo(GFXcanvas16 &c, const VibModel &m) {
  if (m.sd.cardMb > 0)
    textf(c, 0, kInfoY, 1, ui::white, "pomiarow: %lu   karta %lu MB", (unsigned long)m.sd.measurements,
          (unsigned long)m.sd.cardMb);
  else
    text(c, 0, kInfoY, 1, ui::muted, "pomiarow: -   brak karty");
  for (int i = 0; i < 3; ++i) {
    const char axis[2] = {ui::kAxisLetter[i], 0};
    text(c, 230 + 12 * i, kInfoY, 1, ui::kAxisColor[i], axis);
  }
  textf(c, 272, kInfoY, 1, ui::muted, "%.1f s", vib::kBins * vib::kBinMs / 1000.0f);
}

void drawButton(GFXcanvas16 &c, const Rect &r, bool pressed, const char *label) {
  c.fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 6, pressed ? ui::buttonPressed : ui::button);
  c.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 6, ui::edge);
  centered(c, r.x, r.w, r.y + (r.h - 24) / 2, 3, ui::white, label);
}

void drawThreshold(GFXcanvas16 &c, const VibModel &m) {
  drawButton(c, thrui::kMinus, m.pressed == thrui::Button::Minus, "-");
  drawButton(c, thrui::kPlus, m.pressed == thrui::Button::Plus, "+");
  char value[16];
  std::snprintf(value, sizeof value, "%.1f m/s2", m.thresholdDeci / 10.0f);
  // "PROG" (size 1) + gap + value (size 2), centred between the buttons, bottoms aligned.
  const int left = thrui::kMinus.x + thrui::kMinus.w, right = thrui::kPlus.x;
  const int width = 4 * 6 + 6 + int(std::strlen(value)) * 12;
  const int x = left + (right - left - width) / 2;
  const int y = thrui::kMinus.y + (thrui::kMinus.h - 16) / 2;
  text(c, x, y + 8, 1, ui::muted, "PROG");
  text(c, x + 30, y, 2, ui::yellow, value);
}

} // namespace

void drawVibScreen(GFXcanvas16 &c, const VibModel &m) {
  c.fillScreen(ui::bg);
  drawHeader(c, m);
  drawChart(c, m);
  drawInfo(c, m);
  drawThreshold(c, m);
}
