// Renders every screen to PNG-ready raw files on a PC, with the same drawing code
// and Adafruit_GFX as the device. Build and run: see tools/render_screens.sh.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include "StationConfig.h"
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
  const char *names[] = {"line", "accel", "gyro", "disp", "dispplot", "disppath", "wave", "fftx", "ffty", "fftz",
                         "strokes", "web", "battery", "log"};
  // Line: eight tables, the part at table 3; one rest-to-rest move of the meter.
  static line::StationConfig cfg;
  line::parseStationConfig("B1,X+,0,true,none,,\nB2,X+,8,true,none,,\nB3,X+,8,true,none,,\nB4,X+,8,true,lift,Z,0.4\n"
                           "B5,X+,8,true,none,,\nB6,X+,8,true,rotation_z,Z,90\nB7,X+,8,true,none,,\nB8,X+,8,true,none,,\n", cfg);
  static line::Tracker tracker;
  tracker.applyStationConfig(cfg);
  tracker.home();
  m.tracker = &tracker;
  m.configOk = true;
  static DisplacementMeter meter;
  {
    uint32_t us = 0;
    const float g = 9.81f;
    for (int i = 0; i < 1200; ++i) { // 1 s rest, a 0.35 m move along X with a little Y, 1 s rest.
      const float t = (i - 200) / 200.0f;
      float ax = 0, ay = 0;
      if (t >= 0 && t < 1.0f) { // Position 0.35 * (1 - cos(pi t)) / 2: smooth start and stop.
        const float k = 3.14159f;
        ax = 0.35f * 0.5f * k * k * std::cos(k * t);
        ay = 0.1f * 0.5f * k * k * std::sin(2 * k * t) * 0.5f;
      }
      us += 5000;
      meter.add({ax, ay, g}, {}, us);
    }
  }
  m.meter = &meter;
  m.gyroRate = {1.2f, -45.5f, 120.0f};
  m.gyroAngle = {0.3f, -12.0f, 88.0f};
  m.gyroBiasKnown = true;
  static net::Status network;
  std::strcpy(network.ssid, "FABRYKA-WIFI");
  std::strcpy(network.ip, "192.168.1.57");
  std::strcpy(network.hostname, "wibrometr");
  std::strcpy(network.message, "polaczono");
  network.state = net::State::Connected;
  network.clockReady = true;
  m.network = &network;
  m.webClients = 2;
  for (int p = 0; p < kPageCount; ++p) {
    m.pageIndex = p;
    m.page = Page(p);
    drawScreen(canvas, m);
    char file[64];
    std::snprintf(file, sizeof file, "%02d_%s.raw", p, names[p]);
    dump(canvas, file);
  }
  // Variants.
  m.pageIndex = 1; m.page = Page::Accel;
  m.shown = {-19.6f, 15.4f, -38.2f}; m.tared = true;
  drawScreen(canvas, m); dump(canvas, "10_accel_shock.raw");
  m.tared = false; m.live = false; m.sensorStatus = "BRAK QMI8658"; m.sensorError = "WHO_AM_I=0x00, oczekiwano 0x05";
  m.i2cScan = "0x15"; drawScreen(canvas, m); dump(canvas, "11_accel_missing.raw");
  m.live = true; m.pressed = NavButton::Next; m.notice = "TARA USTAWIONA";
  m.shown = {0.12f, -0.31f, 9.81f};
  drawScreen(canvas, m); dump(canvas, "12_pressed_notice.raw");
  m.pressed = NavButton::None; m.notice = nullptr;
  m.pageIndex = 13; m.page = Page::Log;
  m.log.state = LogStatus::State::Recording; m.log.session = 7; m.log.recordingMs = 83000; m.log.fileBytes = 1234567; m.log.samples = 41500; m.log.dropped = 12;
  drawScreen(canvas, m); dump(canvas, "13_log_recording.raw");
  m.log.state = LogStatus::State::NoCard;
  drawScreen(canvas, m); dump(canvas, "14_log_nocard.raw");
  m.log.state = LogStatus::State::Idle; m.keypadOpen = true; std::strcpy(m.keypadDigits, "47");
  drawScreen(canvas, m); dump(canvas, "15_keypad.raw");
  m.keypadOpen = false; m.pageIndex = 12; m.page = Page::Battery;
  m.battery = battery::evaluate(3990, true, true); drawScreen(canvas, m); dump(canvas, "16_battery_charging.raw");
  m.battery = battery::evaluate(900, false, true); drawScreen(canvas, m); dump(canvas, "17_battery_none.raw");
  m.pageIndex = 7; m.page = Page::FftX; m.window = nullptr;
  drawScreen(canvas, m); dump(canvas, "18_fft_collecting.raw");
  // Line variants: not configured, unknown table, moving.
  m.pageIndex = 0; m.page = Page::Line; m.configOk = false; m.configLine = 3; m.configWhat = "kierunek: oczekiwano X+/X-/Y+/Y-";
  drawScreen(canvas, m); dump(canvas, "19_line_noconfig.raw");
  m.configOk = true; tracker.breakSignal();
  m.live = false; m.sensorStatus = "BRAK QMI8658";
  drawScreen(canvas, m); dump(canvas, "20_line_nosensor.raw");
  return 0;
}
