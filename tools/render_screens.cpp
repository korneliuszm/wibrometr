// Renders every screen to PNG-ready raw files on a PC, with the same drawing code
// and Adafruit_GFX as the device. Build and run: see tools/render_screens.sh.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include "ui/Screens.h"

using namespace ui;

namespace {

void dump(GFXcanvas16 &c, const char *name) {
  FILE *f = std::fopen(name, "wb");
  std::fwrite(c.getBuffer(), 2, 320 * 240, f);
  std::fclose(f);
}

struct Data {
  SignalHistory<kBins> history;
  SampleWindow<kFftSize> window;
  StrokeCounter strokes;
  float spectrum[kSpectrumBins];
};

void fill(Data &d) {
  std::mt19937 rng(4);
  std::normal_distribution<float> noise(0, 0.15f);
  // 8 s of a 500 Hz signal: 12 Hz and 60 Hz vibration plus noise on gravity.
  uint32_t us = 0;
  uint32_t binStart = 0;
  for (int i = 0; i < 4000; ++i) {
    const float t = i / 500.0f;
    const Accel a{3.0f * std::sin(2 * 3.14159f * 12 * t) + noise(rng),
                  1.2f * std::sin(2 * 3.14159f * 60 * t) + 0.5f + noise(rng),
                  9.81f + 0.8f * std::sin(2 * 3.14159f * 28 * t) + noise(rng)};
    us += 2000;
    d.window.add(a, us);
    d.history.add(a);
    d.strokes.setBiasTau(1.0f);
    d.strokes.add({1.5f * std::sin(2 * 3.14159f * 2 * t), 0.2f * std::sin(2 * 3.14159f * 2 * t), 9.8f}, us);
    if (us / 1000 >= binStart + kBinMs) { d.history.tick(); binStart += kBinMs; }
  }
}

} // namespace

int main() {
  static Data data;
  fill(data);
  GFXcanvas16 canvas(320, 240);
  Model m;
  m.live = true;
  m.rateHz = 499;
  m.shown = {0.12f, -0.31f, 9.81f};
  m.history = &data.history;
  m.window = &data.window;
  m.spectrum = data.spectrum;
  m.strokes = &data.strokes;
  m.battery = battery::evaluate(3870, false, true);
  m.log.state = LogStatus::State::Idle;
  m.log.cardMb = 15193;
  m.log.session = 6;
  std::strcpy(m.tag, "4711");
  const char *names[] = {"accel", "wave", "fftx", "ffty", "fftz", "strokes", "battery", "log"};
  for (int p = 0; p < kPageCount; ++p) {
    m.pageIndex = p;
    m.page = Page(p);
    drawScreen(canvas, m);
    char file[64];
    std::snprintf(file, sizeof file, "%02d_%s.raw", p, names[p]);
    dump(canvas, file);
  }
  // Variants.
  m.pageIndex = 0; m.page = Page::Accel;
  m.shown = {-19.6f, 15.4f, -38.2f}; m.tared = true;
  drawScreen(canvas, m); dump(canvas, "10_accel_shock.raw");
  m.tared = false; m.live = false; m.sensorStatus = "BRAK QMI8658"; m.sensorError = "WHO_AM_I=0x00, oczekiwano 0x05";
  m.i2cScan = "0x15"; drawScreen(canvas, m); dump(canvas, "11_accel_missing.raw");
  m.live = true; m.pressed = NavButton::Next; m.notice = "TARA USTAWIONA";
  m.shown = {0.12f, -0.31f, 9.81f};
  drawScreen(canvas, m); dump(canvas, "12_pressed_notice.raw");
  m.pressed = NavButton::None; m.notice = nullptr;
  m.pageIndex = 7; m.page = Page::Log;
  m.log.state = LogStatus::State::Recording; m.log.session = 7; m.log.recordingMs = 83000; m.log.fileBytes = 1234567; m.log.samples = 41500; m.log.dropped = 12;
  drawScreen(canvas, m); dump(canvas, "13_log_recording.raw");
  m.log.state = LogStatus::State::NoCard;
  drawScreen(canvas, m); dump(canvas, "14_log_nocard.raw");
  m.log.state = LogStatus::State::Idle; m.keypadOpen = true; std::strcpy(m.keypadDigits, "47");
  drawScreen(canvas, m); dump(canvas, "15_keypad.raw");
  m.keypadOpen = false; m.pageIndex = 6; m.page = Page::Battery;
  m.battery = battery::evaluate(3990, true, true); drawScreen(canvas, m); dump(canvas, "16_battery_charging.raw");
  m.battery = battery::evaluate(900, false, true); drawScreen(canvas, m); dump(canvas, "17_battery_none.raw");
  m.pageIndex = 2; m.page = Page::FftX; m.window = nullptr;
  drawScreen(canvas, m); dump(canvas, "18_fft_collecting.raw");
  return 0;
}
