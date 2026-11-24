// Renders the vibration recorder's screen (src/vibration/VibUi.cpp) in several
// states to raw RGB565 files, with the same drawing code and Adafruit_GFX as the
// device. Build and run: tools/render_vib.sh.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "LiveSpectrum.h"
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
  m.range.distanceMm = 300;
  m.rangeMm = 520;
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
  // Live spectrum: rollers on top (48 Hz on Z), side rollers (31 Hz on X, 120 Hz weak on Y).
  static LiveSpectrum live;
  {
    std::mt19937 rng(5);
    std::normal_distribution<float> noise(0, 0.04f);
    for (int i = 0; i < int(LiveSpectrum::kSize); ++i) {
      const float t = i / 490.0f;
      live.add({0.9f * std::sin(2 * 3.14159f * 31 * t) + noise(rng), 0.3f * std::sin(2 * 3.14159f * 120 * t) + noise(rng),
                1.4f * std::sin(2 * 3.14159f * 48 * t) + noise(rng)},
               uint32_t(i * 1e6f / 490.0f));
    }
    live.compute();
  }
  m.live = &live;
  drawVibScreen(c, m);
  dump(c, "1-armed.raw");

  m.history = &pass;
  m.mode = RecorderCore::Mode::Recording;
  m.sd.state = MeasStatus::State::Recording;
  m.sd.recordingMs = 12300;
  m.sd.number = 13;
  m.level = 2.7f;
  m.rangeState = VibModel::RangeState::Paused;
  drawVibScreen(c, m);
  dump(c, "2-recording.raw");

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
  m.rangeState = VibModel::RangeState::Missing;
  drawVibScreen(c, m);
  dump(c, "4-error.raw");

  // Page 2: peaks of a session, from a few to many measurements.
  static VibPeaks few, many;
  std::mt19937 rng(11);
  std::uniform_real_distribution<float> level(0.8f, 6.0f);
  for (unsigned i = 1; i <= 9; ++i) {
    const float p = level(rng);
    few.add({i, p, {0.9f * p, 0.5f * p, 0.3f * p}});
  }
  for (unsigned i = 1; i <= 47; ++i) {
    const float p = level(rng) * (i == 30 ? 3.0f : 1.0f);
    many.add({i, p, {0.9f * p, 0.5f * p, 0.3f * p}});
  }
  m = VibModel();
  m.sensorLive = true;
  m.sd.state = MeasStatus::State::Idle;
  m.sd.cardMb = 14910;
  m.sd.measurements = 9;
  m.battery = battery::evaluate(4020, false, true);
  m.page = 1;
  drawVibScreen(c, m);
  dump(c, "5-peaks-empty.raw");
  m.peaks = &few;
  drawVibScreen(c, m);
  dump(c, "6-peaks-few.raw");
  m.peaks = &many;
  drawVibScreen(c, m);
  dump(c, "7-peaks-many.raw");

  // Page 3: the spectrum of the last measurement.
  m.page = 2;
  m.peaks = nullptr;
  m.last = nullptr;
  drawVibScreen(c, m);
  dump(c, "8-fft-empty.raw");
  m.last = &stats;
  m.lastNumber = 12;
  drawVibScreen(c, m);
  dump(c, "9-fft.raw");

  // Page 4: range finder settings, with the object at 180 mm (closer than the 300 mm trigger).
  m.page = 3;
  m.rangeState = VibModel::RangeState::Ranging;
  m.rangeMm = 520;
  drawVibScreen(c, m);
  dump(c, "10-range.raw");
  m.rangeMm = 180;
  m.rangePress = {rangeui::Param::Record, +1};
  m.range.mode = 2;
  drawVibScreen(c, m);
  dump(c, "11-range-near.raw");

  // Page 5: USB disk mode switch: no cable, cable (serial), disk mode.
  m.page = 4;
  m.rangePress = {};
  drawVibScreen(c, m);
  dump(c, "12-usb-no-cable.raw");
  m.usbHost = true;
  drawVibScreen(c, m);
  dump(c, "13-usb-serial.raw");
  m.usbDisk = true;
  m.mode = RecorderCore::Mode::Usb;
  m.hostOwns = true;
  m.sd.state = MeasStatus::State::Parked;
  m.usbButton = usbui::Button::Usb;
  drawVibScreen(c, m);
  dump(c, "14-usb-disk.raw");

  // Clearing the card: asked to confirm, then the result.
  m.usbDisk = false;
  m.usbHost = true;
  m.mode = RecorderCore::Mode::Armed;
  m.hostOwns = false;
  m.sd.state = MeasStatus::State::Idle;
  m.usbButton = usbui::Button::None;
  m.cleanConfirm = true;
  drawVibScreen(c, m);
  dump(c, "16-clean-confirm.raw");
  m.cleanConfirm = false;
  m.cleanResult = 284;
  m.sd.measurements = 0;
  drawVibScreen(c, m);
  dump(c, "17-clean-done.raw");
  m.cleanResult = -1;
  m.sd.state = MeasStatus::State::Cleaning;
  m.sd.measurements = 284;
  drawVibScreen(c, m);
  dump(c, "18-cleaning.raw");

  // Touch calibration, third target.
  m.usbDisk = false;
  m.calibStep = 2;
  drawVibScreen(c, m);
  dump(c, "15-calibration.raw");
  return 0;
}
