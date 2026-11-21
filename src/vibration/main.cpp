// Suspension vibration recorder for the Waveshare ESP32-S3-Touch-LCD-2 (env
// waveshare_vibration): QMI8658 at 500 Hz, a measurement file on the TF card each
// time the vibration crosses the threshold.
// Skeleton: sensor task and SD writer only, status on Serial once per second.
// The screen, touch threshold setting and USB disk come in later tasks.
#include <Arduino.h>
#include "RecorderCore.h"
#include "MeasWriter.h"
#include "Qmi8658Decode.h"
#include "VibConfig.h"
#include "VibImuTask.h"
#include "hal/Display.h"
#include "hal/I2cBus.h"

namespace {

hal::Display display;  // Also starts the SPI bus shared with the SD card.
RecorderCore core(TriggerDetector::Config{vib::kSampleRateHz, 1.0f, vib::kWarmupUs, vib::kQuietUs},
                  vib::kPreRollUs);
MeasWriter writer;
VibImuTask imuTask;
QueueHandle_t events = nullptr;

const char *stateName(MeasStatus::State s) {
  switch (s) {
  case MeasStatus::State::NoCard: return "NoCard";
  case MeasStatus::State::Idle: return "Idle";
  case MeasStatus::State::Recording: return "Recording";
  case MeasStatus::State::Error: return "Error";
  case MeasStatus::State::Parked: return "Parked";
  }
  return "?";
}

} // namespace

void setup() {
  Serial.begin(115200);
  hal::I2cBus::begin();
  display.begin();
  events = xQueueCreate(256, sizeof(VibEvent));
  writer.begin(qmi8658::odrHz(qmi8658::odrCode(vib::kSampleRateHz)), qmi8658::rangeG(vib::kRange));
  imuTask.begin(events, &core, &writer);
  Serial.printf("Rejestrator drgan: %.0f Hz, +-%.0f g, prog %.1f m/s2\n", vib::kSampleRateHz,
                qmi8658::rangeG(vib::kRange), core.thresholdDeci() / 10.0f);
}

void loop() {
  static uint32_t lastPrint = 0, samples = 0;
  static Accel last{};
  VibEvent e;
  while (xQueueReceive(events, &e, 0) == pdTRUE) {
    if (e.kind == VibEvent::Kind::Sample) {
      ++samples;
      last = e.dynamic;
    }
  }
  const uint32_t now = millis();
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    const MeasStatus s = writer.status();
    Serial.printf("imu %lu/s dyn %.2f %.2f %.2f | mode %d | SD %s '%s' M%04u %lu smp %lu B %lu ms, "
                  "%lu pomiarow, %lu MB, drop %lu/%lu\n",
                  (unsigned long)samples, last.x, last.y, last.z, int(core.mode()), stateName(s.state),
                  s.message, s.number, (unsigned long)s.samples, (unsigned long)s.fileBytes,
                  (unsigned long)s.recordingMs, (unsigned long)s.measurements, (unsigned long)s.cardMb,
                  (unsigned long)s.dropped, (unsigned long)imuTask.dropped());
    samples = 0;
  }
  delay(10);
}
