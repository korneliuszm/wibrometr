#include "ui/Screens.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Keypad.h"
#include "Qmi8658Decode.h"
#include "UiTheme.h"
#include "ui/Draw.h"
#include "ui/LineScreens.h"

namespace ui {

namespace {

constexpr int W = 320, H = 240;
constexpr int kContentTop = 24;
constexpr int kNoticeY = NavBarLayout::kTop - 12;   // One text line above the bar.
constexpr int kContentBottom = kNoticeY - 2;
const char *const kTitles[] = {"LINIA",   "PRZYSPIESZENIE", "ZYROSKOP", "PRZEMIESZCZENIE", "RUCH W CZASIE",
                               "TOR RUCHU", "PRZEBIEG",     "FFT OS X", "FFT OS Y",        "FFT OS Z",
                               "LICZNIK", "SIEC I WWW",     "AKUMULATOR", "NAGRYWANIE"};
static_assert(sizeof kTitles / sizeof kTitles[0] == kPageCount, "one title per page");

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void drawHeader(GFXcanvas16 &c, const Model &m) {
  text(c, 0, 2, 2, ui::white, kTitles[m.pageIndex % kPageCount]);
  const LogStatus &log = m.log;
  if (log.state == LogStatus::State::Recording) {
    c.fillCircle(188, 9, 4, ui::red);
    const uint32_t s = log.recordingMs / 1000;
    textf(c, 196, 6, 1, ui::white, "%02lu:%02lu", (unsigned long)(s / 60 % 100), (unsigned long)(s % 60));
  } else if (log.state == LogStatus::State::Error) {
    text(c, 188, 6, 1, ui::yellow, "SD !");
  }
  textf(c, 238, 6, 1, ui::white, "%3.0fHz", m.live ? m.rateHz : 0.0f);
  textf(c, 284, 6, 1, ui::muted, "%d/%d", m.pageIndex + 1, kPageCount);
  c.drawFastHLine(0, 20, W, ui::grid);
}

// ------------------------------------------------------------ navigation bar

void button(GFXcanvas16 &c, const Rect &r, bool pressed, bool enabled) {
  c.fillRoundRect(r.x, r.y + 1, r.w, r.h - 2, 5, !enabled ? ui::buttonOff : pressed ? ui::buttonPressed : ui::button);
  c.drawRoundRect(r.x, r.y + 1, r.w, r.h - 2, 5, ui::edge);
}

void centred(GFXcanvas16 &c, const Rect &r, int size, uint16_t color, const char *s) {
  const int w = int(std::strlen(s)) * 6 * size;
  text(c, r.x + (r.w - w) / 2, r.y + (r.h - 8 * size) / 2, size, color, s);
}

void drawNavBar(GFXcanvas16 &c, const Model &m) {
  const ActionLabels a = actionLabels(m);
  const bool navOn = !m.keypadOpen;
  const Rect &prev = NavBarLayout::prev, &next = NavBarLayout::next;
  button(c, prev, m.pressed == NavButton::Prev, navOn);
  button(c, next, m.pressed == NavButton::Next, navOn);
  const uint16_t arrow = navOn ? ui::white : ui::muted;
  c.fillTriangle(prev.x + 32, prev.y + 9, prev.x + 32, prev.y + 25, prev.x + 18, prev.y + 17, arrow);
  c.fillTriangle(next.x + 20, next.y + 9, next.x + 20, next.y + 25, next.x + 34, next.y + 17, arrow);
  button(c, NavBarLayout::action1, m.pressed == NavButton::Action1, a.a1Enabled && a.a1[0]);
  button(c, NavBarLayout::action2, m.pressed == NavButton::Action2, a.a2Enabled && a.a2[0]);
  centred(c, NavBarLayout::action1, 2, a.a1Enabled ? ui::white : ui::muted, a.a1);
  centred(c, NavBarLayout::action2, 2, a.a2Enabled ? a.a2Color : ui::muted, a.a2);
}

// ------------------------------------------------------------------- pages

void drawAccelDiagnostics(GFXcanvas16 &c, const Model &m) {
  text(c, 0, 30, 2, ui::yellow, m.sensorStatus);
  text(c, 0, 52, 1, ui::white, m.sensorError[0] ? m.sensorError : "czekam na pierwsza probe...");
  text(c, 0, 72, 1, ui::muted, "I2C (SDA 48, SCL 47), odpowiada:");
  text(c, 0, 86, 2, ui::white, m.i2cScan[0] ? m.i2cScan : "-");
  text(c, 0, 116, 1, ui::muted, "oczekiwane: 0x6B (IMU QMI8658)");
  text(c, 0, 128, 1, ui::muted, "i 0x15 (dotyk CST816D)");
  text(c, 0, 146, 1, ui::muted, "IMU jest na plytce; sprawdz zasilanie i reset.");
}

void drawAccel(GFXcanvas16 &c, const Model &m) {
  if (!m.live) return drawAccelDiagnostics(c, m);
  constexpr int rowY[3] = {24, 74, 124};
  constexpr int barMid = W / 2, barHalf = 156;
  for (int i = 0; i < 3; ++i) {
    const float v = component(m.shown, i);
    const int y = rowY[i];
    text(c, 2, y, 4, kAxisColor[i], i == 0 ? "X" : i == 1 ? "Y" : "Z");
    textf(c, 34, y, 4, ui::white, "%+7.2f", v);
    text(c, 218, y + 2, 2, ui::muted, "m/s2");
    textf(c, 218, y + 20, 2, ui::white, "%+.2fg", v / qmi8658::kGravity);
    // Bar: centre = 0, ends = +-20 m/s2, clipped.
    const int by = y + 36;
    c.drawFastHLine(0, by + 4, W, ui::grid);
    c.drawFastVLine(barMid, by, 9, ui::muted);
    const int len = int(lroundf(fmaxf(-1.0f, fminf(1.0f, v / kWaveFullScale)) * barHalf));
    if (len >= 0) c.fillRect(barMid, by + 1, len + 1, 7, kAxisColor[i]);
    else c.fillRect(barMid + len, by + 1, -len, 7, kAxisColor[i]);
  }
  const Accel &a = m.shown;
  const float mag = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  textf(c, 0, 174, 2, ui::muted, "|a| %.2f m/s2  %.2f g", mag, mag / qmi8658::kGravity);
  if (m.tared) text(c, 270, 174, 2, ui::yellow, "TARA");
}

// Plot frame shared by waveform and spectrum.
constexpr int kPlotX = 34, kPlotY = 40, kPlotW = kBins * 2, kPlotH = 128;

void drawPlotGrid(GFXcanvas16 &c) {
  for (int n = 0; n <= 4; ++n) {
    const int y = kPlotY + n * (kPlotH - 1) / 4;
    c.drawFastHLine(kPlotX, y, kPlotW, ui::grid);
    const int x = kPlotX + n * (kPlotW - 1) / 4;
    c.drawFastVLine(x, kPlotY, kPlotH, ui::grid);
  }
}

const char *liveState(const Model &m) { return m.live ? (m.paused ? "PAUZA" : "NA ZYWO") : m.sensorStatus; }

void drawWave(GFXcanvas16 &c, const Model &m) {
  text(c, 0, 28, 1, m.live ? ui::green : ui::yellow, liveState(m));
  text(c, 96, 28, 1, kAxisColor[0], "X");
  text(c, 108, 28, 1, kAxisColor[1], "Y");
  text(c, 120, 28, 1, kAxisColor[2], "Z");
  text(c, 136, 28, 1, ui::muted, "m/s2");
  text(c, 190, 28, 1, ui::muted, m.zeroed ? "OFFSET" : "PELNY");
  text(c, 0, 182, 1, ui::muted, "przytrzymaj PAUZA: zero / pelny odczyt");
  text(c, 0, kPlotY, 1, ui::muted, "+20");
  text(c, 16, kPlotY + kPlotH / 2 - 3, 1, ui::muted, "0");
  text(c, 0, kPlotY + kPlotH - 8, 1, ui::muted, "-20");
  drawPlotGrid(c);
  bool clipped = false;
  const auto yPixel = [](float v) {
    v = fmaxf(-kWaveFullScale, fminf(kWaveFullScale, v));
    return kPlotY + int(lroundf((kWaveFullScale - v) * (kPlotH - 1) / (2 * kWaveFullScale)));
  };
  if (m.history) {
    const SignalHistory<kBins> &h = *m.history;
    for (int ch = 0; ch < 3; ++ch) {
      bool previous = false;
      int prevX = 0, prevY = 0;
      for (std::size_t i = 0; i < h.size(); ++i) {
        const SignalBin b = h.at(i);
        const int x = kPlotX + int(kBins - h.size() + i) * 2;
        if (!b.valid) { previous = false; continue; }
        const float lo = component(b.low, ch), hi = component(b.high, ch);
        clipped |= lo < -kWaveFullScale || hi > kWaveFullScale;
        const int top = yPixel(hi), bottom = yPixel(lo);
        if (previous) c.drawLine(prevX, prevY, x, yPixel(component(b.first, ch)), kAxisColor[ch]);
        c.drawFastVLine(x, top, bottom - top + 1, kAxisColor[ch]);
        prevX = x;
        prevY = yPixel(component(b.last, ch));
        previous = true;
      }
    }
  }
  textf(c, kPlotX, kPlotY + kPlotH + 4, 1, ui::muted, "-%.1fs", kBins * kBinMs / 1000.0f);
  text(c, kPlotX + kPlotW - 12, kPlotY + kPlotH + 4, 1, ui::muted, "0s");
  if (clipped) text(c, 120, kPlotY + kPlotH + 4, 1, ui::yellow, "POZA SKALA");
  else if (m.droppedSamples) text(c, 120, kPlotY + kPlotH + 4, 1, ui::yellow, "UTRATA PROBEK");
}

void drawSpectrum(GFXcanvas16 &c, const Model &m, int ch) {
  const bool full = m.window && m.window->full();
  text(c, 0, 28, 1, m.live || m.paused ? (m.paused ? ui::yellow : ui::green) : ui::yellow,
       m.live || m.paused ? (m.paused ? "PAUZA" : "NA ZYWO") : m.sensorStatus);
  text(c, 190, 28, 1, ui::muted, "amplituda m/s2");
  drawPlotGrid(c);
  if (!(m.live || m.paused) || !full || !m.spectrum) {
    c.setTextColor(ui::muted);
    c.setTextSize(2);
    c.setCursor(kPlotX + 40, kPlotY + kPlotH / 2 - 8);
    if (m.live && m.window) c.printf("ZBIERANIE %u/%u", unsigned(m.window->size()), unsigned(kFftSize));
    else c.print("BRAK DANYCH");
    return;
  }
  amplitudeSpectrum(*m.window, ch, *reinterpret_cast<float(*)[kSpectrumBins]>(m.spectrum));
  const float rate = m.window->rateHz();
  std::size_t best = 1;
  for (std::size_t k = 1; k < kSpectrumBins; ++k)
    if (m.spectrum[k] > m.spectrum[best]) best = k;
  textf(c, 56, 28, 1, ui::white, "max %.1f Hz  %.2f", best * rate / kFftSize, m.spectrum[best]);
  textf(c, 0, kPlotY, 1, ui::muted, "%.0f", kFftRange);
  text(c, 16, kPlotY + kPlotH - 8, 1, ui::muted, "0");
  // One column per pixel: the largest bin that falls on it (bin 0 is the removed DC).
  int prevX = 0, prevY = 0;
  for (int px = 0; px < kPlotW; ++px) {
    const std::size_t k0 = 1 + std::size_t(px) * (kSpectrumBins - 1) / kPlotW;
    std::size_t k1 = 1 + std::size_t(px + 1) * (kSpectrumBins - 1) / kPlotW;
    if (k1 <= k0) k1 = k0 + 1;
    float peak = 0;
    for (std::size_t k = k0; k < k1 && k < kSpectrumBins; ++k) peak = fmaxf(peak, m.spectrum[k]);
    const int y = kPlotY + kPlotH - 1 - int(lroundf(fminf(peak / kFftRange, 1.0f) * (kPlotH - 1)));
    if (px > 0) c.drawLine(prevX, prevY, kPlotX + px, y, kAxisColor[ch]);
    prevX = kPlotX + px;
    prevY = y;
  }
  text(c, kPlotX, kPlotY + kPlotH + 4, 1, ui::muted, "0");
  textf(c, kPlotX + kPlotW / 2 - 12, kPlotY + kPlotH + 4, 1, ui::muted, "%.0f", rate / 4);
  textf(c, kPlotX + kPlotW - 36, kPlotY + kPlotH + 4, 1, ui::muted, "%.0fHz", rate / 2);
}

void drawStrokes(GFXcanvas16 &c, const Model &m) {
  if (!m.strokes) return;
  const StrokeCounter &s = *m.strokes;
  text(c, 0, 28, 1, m.live ? ui::green : ui::yellow, m.live ? "NA ZYWO" : m.sensorStatus);
  textf(c, 0, 44, 8, ui::white, "%lu", (unsigned long)s.count());
  textf(c, 190, 48, 3, s.perMinute() > 0 ? ui::green : ui::muted, "%.0f/min", s.perMinute());
  text(c, 190, 80, 1, ui::muted, "pelnych ruchow");
  text(c, 190, 92, 1, ui::muted, "(tam i z powrotem)");
  textf(c, 0, 116, 1, ui::muted, "kierunek X%+.1f Y%+.1f Z%+.1f", s.axis(0), s.axis(1), s.axis(2));
  // Velocity-like signal against the threshold: +-envelope spans the bar.
  constexpr int barX = 2, barW = W - 4, barY = 132, barH = 18, mid = barX + barW / 2;
  const float range = fmaxf(s.envelope(), 0.1f);
  c.drawRect(barX, barY, barW, barH, ui::muted);
  const int tick = int(lroundf(fminf(s.threshold() / range, 1.0f) * (barW / 2 - 2)));
  c.drawFastVLine(mid - tick, barY, barH, ui::yellow);
  c.drawFastVLine(mid + tick, barY, barH, ui::yellow);
  const int len = int(lroundf(fmaxf(-1.0f, fminf(1.0f, s.filtered() / range)) * (barW / 2 - 2)));
  if (len >= 0) c.fillRect(mid, barY + 2, len, barH - 4, kAxisColor[0]);
  else c.fillRect(mid + len, barY + 2, -len, barH - 4, kAxisColor[0]);
  textf(c, 0, 158, 1, ui::muted, "prog %.2f m/s   szczyt %.2f m/s", s.threshold(), s.envelope());
  text(c, 0, 172, 1, ui::muted, "uwaga: akcelerometr bez fuzji, grawitacja wplywa");
  text(c, 0, 184, 1, ui::muted, "na wynik przy obracaniu reki");
}

void drawBattery(GFXcanvas16 &c, const Model &m) {
  const battery::Reading &b = m.battery;
  if (b.state == battery::State::NoBattery) {
    text(c, 0, 34, 2, ui::yellow, "BRAK AKUMULATORA");
    textf(c, 0, 60, 2, ui::white, "ADC: %.2f V", b.millivolts / 1000.0f);
    text(c, 0, 90, 1, ui::muted, "Podlacz ogniwo do zlacza MX1.25 (BAT).");
    text(c, 0, 102, 1, ui::muted, "Zasilanie z USB nie pokaze napiecia ogniwa.");
    return;
  }
  const bool charging = b.state == battery::State::Charging;
  textf(c, 0, 32, 6, ui::white, "%s%.0f%%", charging ? "~" : "", fminf(b.percent, 100.0f));
  textf(c, 196, 38, 3, ui::white, "%.2fV", b.millivolts / 1000.0f);
  constexpr int barW = W - 4;
  c.drawRect(0, 88, barW + 4, 24, ui::muted);
  const int fill = int(lroundf(fmaxf(0.0f, fminf(b.percent, 100.0f)) * barW / 100));
  c.fillRect(2, 90, fill, 20, b.percent < 20 ? ui::red : ui::green);
  const char *state = charging ? "LADOWANIE" : b.state == battery::State::Full ? "NALADOWANY" : "NA AKUMULATORZE";
  text(c, 0, 124, 3, charging ? ui::green : ui::white, state);
  if (m.chargeKnown) {
    text(c, 0, 158, 1, ui::muted, "ladowarka ETA6096, linia stanu CHG_DET (Rev 2.0)");
  } else {
    text(c, 0, 158, 1, ui::yellow, "Rev 1.0: brak wykrywania ladowania, tylko napiecie");
  }
  text(c, 0, 170, 1, ui::muted, charging ? "podczas ladowania procent jest przyblizony" : "procent z napiecia ogniwa (bez ukladu pomiarowego)");
}

void drawKeypad(GFXcanvas16 &c, const Model &m) {
  text(c, 0, 26, 1, ui::muted, "Numer zawieszki:");
  c.drawRect(0, 36, W, 18, ui::edge);
  textf(c, 6, 38, 2, ui::white, "%s_", m.keypadDigits);
  for (int row = 0; row < KeypadLayout::kRows; ++row)
    for (int col = 0; col < KeypadLayout::kCols; ++col) {
      const Rect r = KeypadLayout::rect(col, row);
      const Key k = KeypadLayout::keyAt(col, row);
      c.fillRoundRect(r.x, r.y, r.w, r.h, 5, ui::button);
      c.drawRoundRect(r.x, r.y, r.w, r.h, 5, ui::edge);
      char label[4] = "";
      if (k == Key::Backspace) std::snprintf(label, sizeof label, "<-");
      else if (k == Key::Ok) std::snprintf(label, sizeof label, "OK");
      else std::snprintf(label, sizeof label, "%d", int(k));
      centred(c, r, 2, k == Key::Ok ? ui::green : ui::white, label);
    }
}

void drawLog(GFXcanvas16 &c, const Model &m) {
  if (m.keypadOpen) return drawKeypad(c, m);
  const LogStatus &s = m.log;
  switch (s.state) {
  case LogStatus::State::NoCard:
    text(c, 0, 30, 2, ui::yellow, "BRAK KARTY SD");
    text(c, 0, 54, 1, ui::muted, "Wloz karte TF (FAT32), potem mozna nagrywac.");
    break;
  case LogStatus::State::Error:
    text(c, 0, 30, 2, ui::red, "BLAD KARTY SD");
    text(c, 0, 54, 1, ui::white, s.message);
    text(c, 0, 66, 1, ui::muted, "Ponawiam probe co 3 s.");
    break;
  case LogStatus::State::Idle:
    text(c, 0, 30, 2, ui::green, "KARTA GOTOWA");
    textf(c, 0, 54, 1, ui::muted, "%lu MB, pliki /LOGS/Sxxxx.CSV", (unsigned long)s.cardMb);
    if (s.session) textf(c, 0, 66, 1, ui::muted, "ostatnia sesja: S%04u", s.session);
    break;
  case LogStatus::State::Recording: {
    const uint32_t sec = s.recordingMs / 1000;
    c.fillCircle(8, 38, 7, ui::red);
    textf(c, 24, 30, 2, ui::white, "S%04u  %02lu:%02lu", s.session, (unsigned long)(sec / 60 % 100), (unsigned long)(sec % 60));
    textf(c, 0, 54, 1, ui::muted, "rozmiar pliku: %.1f kB", s.fileBytes / 1024.0f);
    textf(c, 0, 66, 1, ui::muted, "probek: %lu", (unsigned long)s.samples);
    if (s.dropped) textf(c, 0, 78, 1, ui::yellow, "utracone probki: %lu", (unsigned long)s.dropped);
    break;
  }
  }
  // Tag line: tap it (or the button below) to type another number.
  c.drawRect(0, 100, W, 40, ui::edge);
  text(c, 6, 106, 1, ui::muted, "NUMER ZAWIESZKI (do naglowka pliku)");
  if (m.tag[0]) textf(c, 6, 120, 2, ui::white, "%s", m.tag);
  else text(c, 6, 120, 2, ui::muted, "dotknij, aby wpisac");
  text(c, 0, 150, 1, ui::muted, "Numer trafia do naglowka pliku przy starcie.");
  text(c, 0, 162, 1, ui::muted, "Kolumny: t_us, ax, ay, az (m/s2).");
  text(c, 0, 174, 1, ui::muted, "Wiersze z # to zdarzenia (np. bateria).");
}

void drawNotice(GFXcanvas16 &c, const Model &m) {
  if (!m.notice) return;
  c.fillRect(0, kNoticeY - 1, W, 12, ui::bg);
  const int w = int(std::strlen(m.notice)) * 6;
  text(c, clampi((W - w) / 2, 0, W), kNoticeY + 1, 1, ui::yellow, m.notice);
}

} // namespace

ActionLabels actionLabels(const Model &m) {
  ActionLabels a;
  const bool recording = m.log.state == LogStatus::State::Recording;
  const bool card = m.log.state == LogStatus::State::Idle || recording;
  a.a2 = recording ? "STOP" : "REC";
  a.a2Color = recording ? ui::white : ui::red;
  a.a2Enabled = card;
  switch (m.page) {
  case Page::Line: a.a1 = "STOL 1"; a.a1Enabled = m.configOk; break;
  case Page::Gyro: a.a1 = "KAT 0"; break;
  case Page::Disp:
  case Page::DispPlot:
  case Page::DispPath: a.a1 = "SUMA 0"; break;
  case Page::Accel: a.a1 = m.tared ? "TARA OFF" : "TARA"; break;
  case Page::Wave:
  case Page::FftX:
  case Page::FftY:
  case Page::FftZ: a.a1 = m.paused ? "WZNOW" : "PAUZA"; break;
  case Page::Strokes: a.a1 = "KASUJ"; break;
  case Page::Log: a.a1 = "TAG"; a.a1Enabled = !recording; break;
  default: break;
  }
  if (m.keypadOpen) {
    a.a1 = "ANULUJ";
    a.a1Enabled = true;
    a.a2 = "OK";
    a.a2Color = ui::green;
    a.a2Enabled = true;
  }
  return a;
}

void drawScreen(GFXcanvas16 &c, const Model &m) {
  c.fillScreen(ui::bg);
  c.setTextWrap(false);
  drawHeader(c, m);
  switch (m.page) {
  case Page::Line: drawLinePage(c, m); break;
  case Page::Gyro: drawGyroPage(c, m); break;
  case Page::Disp: drawDisplacementPage(c, m); break;
  case Page::DispPlot: drawDisplacementPlotPage(c, m); break;
  case Page::DispPath: drawDisplacementPathPage(c, m); break;
  case Page::Web: drawWebPage(c, m); break;
  case Page::Accel: drawAccel(c, m); break;
  case Page::Wave: drawWave(c, m); break;
  case Page::FftX: drawSpectrum(c, m, 0); break;
  case Page::FftY: drawSpectrum(c, m, 1); break;
  case Page::FftZ: drawSpectrum(c, m, 2); break;
  case Page::Strokes: drawStrokes(c, m); break;
  case Page::Battery: drawBattery(c, m); break;
  case Page::Log: drawLog(c, m); break;
  default: break;
  }
  drawNotice(c, m);
  drawNavBar(c, m);
}

} // namespace ui
