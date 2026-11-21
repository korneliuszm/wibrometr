#include "VibImuTask.h"
#include <cstdio>
#include "SampleClock.h"
#include "VibConfig.h"
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"
#include "hal/Qmi8658.h"

void VibImuTask::begin(QueueHandle_t events, RecorderCore *core, MeasSink *sink) {
  events_ = events;
  core_ = core;
  sink_ = sink;
  // Same core and priority as the line firmware's sensor task; above the SD writer
  // (priority 1 on core 0), so a slow card never delays sampling.
  xTaskCreatePinnedToCore(entry, "vibimu", 6144, this, 3, nullptr, 0);
}

void VibImuTask::emit(VibEvent::Kind kind, const Accel &dynamic) {
  if (!events_) return;
  VibEvent e;
  e.kind = kind;
  e.ms = millis();
  e.dynamic = dynamic;
  if (xQueueSend(events_, &e, 0) != pdTRUE) ++dropped_;
}

void VibImuTask::run() {
  hal::Qmi8658 imu;
  SampleClock clock;
  bool ready = false;
  int errors = 0;
  uint32_t retryAt = 0, rateStart = 0, rateCount = 0, highRateSeconds = 0;
  const uint32_t periodUs = uint32_t(1e6f / vib::kSampleRateHz + 0.5f);
  RecorderCore &core = *core_;
  MeasSink &sink = *sink_;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    // USB hand-over requests are applied here even when no samples arrive.
    core.service(sink);
    if (!ready) {
      const uint32_t now = millis();
      if (int32_t(now - retryAt) >= 0) {
        retryAt = now + 2000;
        emit(VibEvent::Kind::Connecting);
        hal::I2cBus::recover();
        hal::Qmi8658::Config config;
        config.range = vib::kRange;
        config.odrHz = vib::kSampleRateHz;
        config.lowPassMode = vib::kLowPassMode;
        config.gyro = false;  // Not recorded; halves the I2C read per sample.
        if (imu.begin(config)) {
          sampleRateHz_ = imu.sampleRateHz();
          rangeG_ = imu.rangeG();
          Serial.printf("IMU: QMI8658 ready, %.1f Hz, +-%.0f g\n", sampleRateHz_.load(), rangeG_.load());
          ready = true;
          errors = 0;
          clock.reset();
          core.onGap(sink);  // Fresh stream: pre-roll and filter start over (warm-up).
          rateStart = millis();
          rateCount = highRateSeconds = 0;
        } else {
          // List what does answer on the bus, so wiring problems are visible.
          char scan[40] = "";
          size_t n = 0;
          for (uint8_t addr = 0x08; addr < 0x78 && n + 6 < sizeof scan; ++addr)
            if (hal::I2cBus::probe(addr))
              n += std::snprintf(scan + n, sizeof scan - n, "%s0x%02X", n ? " " : "", addr);
          Serial.printf("IMU: not found (%s); bus: %s\n", imu.error(), n ? scan : "brak");
          emit(VibEvent::Kind::Waiting);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      wake = xTaskGetTickCount();
      continue;
    }
    vTaskDelayUntil(&wake, 1);  // 1 kHz polling.
    Accel a, w;
    switch (imu.poll(a, w)) {
    case hal::Qmi8658::Poll::Sample: {
      errors = 0;
      const SampleClock::Stamp stamp = clock.stamp(micros(), periodUs);
      core.onSample(stamp.us, a, sink);  // Sensor axes, no mount correction (spec).
      emit(VibEvent::Kind::Sample, core.dynamic());
      ++rateCount;
      break;
    }
    case hal::Qmi8658::Poll::Error:
      if (++errors >= 20) {
        ready = false;
        retryAt = millis() + 500;
        core.onGap(sink);  // Closes a running measurement.
        Serial.println("IMU: I2C read errors, reconnecting");
        emit(VibEvent::Kind::Waiting);
      }
      break;
    default:
      break;
    }
    const uint32_t now = millis();
    if (now - rateStart >= 1000) {
      // More samples than the ODR means the data-ready flag is not clearing on read.
      if (rateCount > uint32_t(vib::kSampleRateHz * 1.3f) && ++highRateSeconds == 3)
        Serial.printf("IMU: %lu samples/s for %.0f Hz ODR: duplicates? (status flag not cleared by reading)\n",
                      (unsigned long)rateCount, vib::kSampleRateHz);
      rateStart = now;
      rateCount = 0;
    }
  }
}
