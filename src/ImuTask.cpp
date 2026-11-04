#include "ImuTask.h"
#include <cstdio>
#include "AppConfig.h"
#include "SampleClock.h"
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"
#include "hal/Qmi8658.h"

void ImuTask::begin(QueueHandle_t events, hal::SdLogger *logger) {
  events_ = events;
  logger_ = logger;
  xTaskCreatePinnedToCore(entry, "imu", 6144, this, 3, nullptr, 0);
}

ImuDiagnostics ImuTask::diagnostics() const {
  ImuDiagnostics d;
  portENTER_CRITICAL(&mux_);
  d = diag_;
  portEXIT_CRITICAL(&mux_);
  return d;
}

void ImuTask::emit(ImuEvent::Kind kind, uint32_t us, const Accel &a) {
  ImuEvent e;
  e.kind = kind;
  e.ms = millis();
  e.us = us;
  e.value = a;
  if (xQueueSend(events_, &e, 0) != pdTRUE) ++dropped_;
}

void ImuTask::run() {
  hal::Qmi8658 imu;
  SampleClock clock;
  bool ready = false;
  int errors = 0;
  uint32_t retryAt = 0, rateStart = 0, rateCount = 0, highRateSeconds = 0;
  const uint32_t periodUs = uint32_t(1e6f / app::kSampleRateHz + 0.5f);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (!ready) {
      const uint32_t now = millis();
      if (int32_t(now - retryAt) >= 0) {
        retryAt = now + 2000;
        emit(ImuEvent::Kind::Connecting);
        hal::I2cBus::recover();
        hal::Qmi8658::Config config;
        config.range = app::kRange;
        config.odrHz = app::kSampleRateHz;
        config.lowPassMode = app::kLowPassMode;
        if (imu.begin(config)) {
          sampleRateHz_ = imu.sampleRateHz();
          rangeG_ = imu.rangeG();
          Serial.printf("IMU: QMI8658 ready, %.1f Hz, +-%.0f g\n", sampleRateHz_, rangeG_);
          ready = true;
          errors = 0;
          clock.reset();
          rateStart = millis();
          rateCount = highRateSeconds = 0;
        } else {
          ImuDiagnostics d;
          std::snprintf(d.error, sizeof d.error, "%s", imu.error());
          // List what does answer on the bus, so wiring problems are visible.
          size_t n = 0;
          d.i2cScan[0] = '\0';
          for (uint8_t addr = 0x08; addr < 0x78 && n + 6 < sizeof d.i2cScan; ++addr)
            if (hal::I2cBus::probe(addr)) n += std::snprintf(d.i2cScan + n, sizeof d.i2cScan - n, "%s0x%02X", n ? " " : "", addr);
          if (!n) std::snprintf(d.i2cScan, sizeof d.i2cScan, "brak");
          portENTER_CRITICAL(&mux_);
          diag_ = d;
          portEXIT_CRITICAL(&mux_);
          Serial.printf("IMU: not found (%s); bus: %s\n", d.error, d.i2cScan);
          emit(ImuEvent::Kind::Waiting);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      wake = xTaskGetTickCount();
      continue;
    }
    vTaskDelayUntil(&wake, 1); // 1 kHz polling.
    Accel a;
    switch (imu.poll(a)) {
    case hal::Qmi8658::Poll::Sample: {
      errors = 0;
      const SampleClock::Stamp stamp = clock.stamp(micros(), periodUs);
      emit(ImuEvent::Kind::Sample, stamp.us, a);
      if (logger_) logger_->pushSample(stamp.us, a);
      ++rateCount;
      break;
    }
    case hal::Qmi8658::Poll::Error:
      if (++errors >= 20) {
        ready = false;
        retryAt = millis() + 500;
        Serial.println("IMU: I2C read errors, reconnecting");
        emit(ImuEvent::Kind::Waiting);
      }
      break;
    default:
      break;
    }
    const uint32_t now = millis();
    if (now - rateStart >= 1000) {
      // More samples than the ODR means the data-ready flag is not clearing on read.
      if (rateCount > uint32_t(app::kSampleRateHz * 1.3f) && ++highRateSeconds == 3)
        Serial.printf("IMU: %lu samples/s for %.0f Hz ODR: duplicates? (status flag not cleared by reading)\n",
                      (unsigned long)rateCount, app::kSampleRateHz);
      rateStart = now;
      rateCount = 0;
    }
  }
}
