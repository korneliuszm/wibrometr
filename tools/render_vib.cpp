// Renders the vibration recorder's screen (src/vibration/VibUi.cpp) in several
// states to raw RGB565 files, with the same drawing code and Adafruit_GFX as the
// device. Build and run: tools/render_vib.sh.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "VibUi.h"

namespace {

void dump(GFXcanvas16 &c, const char *name) {
  FILE *f = std::fopen(name, "wb");
  std::fwrite(c.getBuffer(), 2, 320 * 240, f);
  std::fclose(f);
}

// 5.4 s of the dynamic acceleration: a pass (decaying 12 Hz / 30 Hz burst) in the
// last seconds, sensor noise before it.
void fill(SignalHistory<vib::kBins> &h, float amplitude) {
  std::mt19937 rng(7);
  std::normal_distribution<float> noise(0, 0.06f);
  const int samples = vib::kBins * 20;  // 20 samples (40 ms at 500 Hz) per bin.
  for (int i = 0; i < samples; ++i) {
    const float t = i / 500.0f;
    const float onset = 3.2f;
    const float env = t < onset ? 0 : amplitude * std::exp(-(t - onset) * 0.9f);
    const Accel a{env * std::sin(2 * 3.14159f * 12 * t) + noise(rng),
                  0.6f * env * std::sin(2 * 3.14159f * 30 * t + 1) + noise(rng),
                  0.4f * env * std::sin(2 * 3.14159f * 7 * t + 2) + noise(rng)};
    h.add(a);
    if (i % 20 == 19) h.tick();
  }
}

} // namespace

int main() {
  GFXcanvas16 c(320, 240);
  static SignalHistory<vib::kBins> quiet, pass;
  fill(quiet, 0);
  fill(pass, 3.5f);

  VibModel m;
  m.sensorLive = true;
  m.thresholdDeci = 10;
  m.sd.state = MeasStatus::State::Idle;
  m.sd.cardMb = 14910;
  m.sd.measurements = 12;
  m.battery = battery::evaluate(4020, false, true);
  m.nowMs = 1000;

  m.history = &quiet;
  m.level = 0.08f;
  drawVibScreen(c, m);
  dump(c, "0-no-measurement.raw");

  // The last measurement: 1 s pre-roll noise, then a decaying 48 Hz / 120 Hz burst.
  static MeasStats stats;
  {
    std::mt19937 rng(3);
    std::normal_distribution<float> noise(0, 0.05f);
    std::vector<Accel> v;
    for (int i = 0; i < 6000; ++i) {
      const float t = i / 512.0f, env = t < 1 ? 0 : 3.2f * std::exp(-(t - 1) * 0.35f);
      v.push_back({0.4f + env * std::sin(2 * 3.14159f * 48 * t) + noise(rng),
                   -0.2f + 0.5f * env * std::sin(2 * 3.14159f * 120 * t) + noise(rng),
                   9.81f + 0.3f * env * std::sin(2 * 3.14159f * 12 * t) + noise(rng)});
    }
    stats = measstats::compute(v.data(), v.size(), 512.0f);
  }
  m.last = &stats;
  m.lastNumber = 12;
  drawVibScreen(c, m);
  dump(c, "1-armed.raw");

  m.history = &pass;
  m.mode = RecorderCore::Mode::Recording;
  m.sd.state = MeasStatus::State::Recording;
  m.sd.recordingMs = 12300;
  m.sd.number = 13;
  m.level = 2.7f;
  m.pressed = thrui::Button::Plus;
  drawVibScreen(c, m);
  dump(c, "2-recording.raw");

  m.pressed = thrui::Button::None;
  m.mode = RecorderCore::Mode::Usb;
  m.hostOwns = true;
  m.sd.state = MeasStatus::State::Parked;
  m.history = &quiet;
  m.level = 0;
  m.battery = battery::evaluate(4110, true, true);
  m.charging = true;
  drawVibScreen(c, m);
  dump(c, "3-usb.raw");

  m.mode = RecorderCore::Mode::Armed;
  m.hostOwns = false;
  m.charging = false;
  m.battery = battery::evaluate(3600, false, true);
  m.sd.state = MeasStatus::State::Error;
  std::snprintf(m.sd.message, sizeof m.sd.message, "blad zapisu na karte");
  m.sd.recoveries = 2;
  m.history = &pass;
  m.level = 0.3f;
  m.thresholdDeci = 25;
  drawVibScreen(c, m);
  dump(c, "4-error.raw");
  return 0;
}
