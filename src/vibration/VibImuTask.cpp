#include "VibImuTask.h"
#include <cstdio>
#include "FifoClock.h"
#include "CountedClock.h"
#include "VibConfig.h"
#include "hal/BoardPins.h"
#include "hal/I2cBus.h"
#include "hal/Qmi8658.h"


void VibImuTask::begin(QueueHandle_t events, RecorderCore *core, MeasSink *sink) {
  events_ = events;
  core_ = core;
  sink_ = sink;
  // Core 0, priority 23: just below the TinyUSB task (24). At priority 3 the USB
  // task (disk access through SPI while a computer is connected) and the others
  // delayed polls enough to lose ~10 % of the samples; at 23 none were lost
  // (measured with the chip's sample counter). The task sleeps between polls, so
  // lower-priority work (SD writer, touch) still runs. Core 1 is worse: the
  // screen flush there cost ~45 % of the samples.
  xTaskCreatePinnedToCore(entry, "vibimu", 6144, this, 23, nullptr, 0);
}

void VibImuTask::emit(VibEvent::Kind kind, const Accel &dynamic, uint32_t ms) {
  if (!events_) return;
  VibEvent e;
  e.kind = kind;
  e.ms = ms ? ms : millis();
  e.dynamic = dynamic;
  if (xQueueSend(events_, &e, 0) != pdTRUE) ++dropped_;
}

void VibImuTask::run() {
  hal::Qmi8658 imu;
  FifoClock clock(vib::kSampleRateHz);
  CountedClock pollClock(vib::kSampleRateHz);
  static Accel batch[hal::Qmi8658::kFifoSamples];
  bool ready = false;
  int errors = 0;
  uint32_t retryAt = 0, rateStart = 0, rateCount = 0, highRateSeconds = 0;
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
        config.lowPass = vib::kLowPass;
        config.lowPassMode = vib::kLowPassMode;
        config.gyro = false;  // Not recorded; the FIFO then holds accelerometer samples only.
        config.fifo = vib::kUseFifo;
        if (imu.begin(config)) {
          sampleRateHz_ = imu.sampleRateHz();
          rangeG_ = imu.rangeG();
          Serial.printf("IMU: QMI8658 ready, %s, %.1f Hz nominal, +-%.0f g, low-pass %s\n", vib::kUseFifo ? "FIFO" : "polled", sampleRateHz_.load(),
                        rangeG_.load(), vib::kLowPass ? "on" : "off");
          ready = true;
          errors = 0;
          clock.reset();
          pollClock.reset();
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
    if (!vib::kUseFifo) {
      // Counter + data polled every 1 ms (two polls per 500 Hz sample). The chip's
      // sample counter tells exactly how many samples a late poll lost.
      vTaskDelayUntil(&wake, 1);
      Accel a;
      uint32_t skipped = 0;
      const hal::Qmi8658::Poll p = imu.pollCounted(a, skipped);
      if (p == hal::Qmi8658::Poll::Sample) {
        errors = 0;
        const uint32_t t = pollClock.stamp(micros(), skipped);
        if (skipped) {
          missed_ += skipped;
          if (core.mode() == RecorderCore::Mode::Recording) sink.missed(skipped);
        }
        if (pollClock.rateMeasured()) measuredHz_ = pollClock.rateHz();
        core.onSample(t, a, sink);  // Sensor axes, no mount correction (spec).
        emit(VibEvent::Kind::Sample, core.dynamic(), t / 1000);
        ++rateCount;
      } else if (p == hal::Qmi8658::Poll::Error && ++errors >= 20) {
        ready = false;
        retryAt = millis() + 500;
        core.onGap(sink);  // Closes a running measurement.
        Serial.println("IMU: I2C read errors, reconnecting");
        emit(VibEvent::Kind::Waiting);
      }
    } else {  // FIFO path (vib::kUseFifo): loses samples, kept for reference.
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(vib::kFifoReadMs));
      uint32_t readUs = 0;
      bool overflow = false;
      const int n = imu.readFifo(batch, hal::Qmi8658::kFifoSamples, readUs, overflow);
      if (n < 0) {
        if (++errors >= 20) {
          ready = false;
          retryAt = millis() + 500;
          core.onGap(sink);  // Closes a running measurement.
          Serial.println("IMU: I2C read errors, reconnecting");
          emit(VibEvent::Kind::Waiting);
        }
      } else if (n > 0) {
        errors = 0;
        const FifoClock::Batch b = clock.batch(readUs, uint32_t(n));
        if (overflow) ++overflows_;  // Chip flag; the count of lost samples comes from the clock.
        if (b.missed) {
          missed_ += b.missed;
          if (core.mode() == RecorderCore::Mode::Recording) sink.missed(b.missed);
        }
        if (clock.rateMeasured()) measuredHz_ = clock.rateHz();
        for (int i = 0; i < n; ++i) {
          const uint32_t t = b.at(uint32_t(i));
          core.onSample(t, batch[i], sink);  // Sensor axes, no mount correction (spec).
          emit(VibEvent::Kind::Sample, core.dynamic(), t / 1000);
        }
        rateCount += uint32_t(n);
      }
    }
    const uint32_t now = millis();
    if (now - rateStart >= 1000) {
      // Far more samples than the ODR: the FIFO count is not what it seems.
      if (rateCount > uint32_t(vib::kSampleRateHz * 1.3f) && ++highRateSeconds == 3)
        Serial.printf("IMU: %lu samples/s for %.0f Hz ODR: FIFO count wrong?\n", (unsigned long)rateCount,
                      vib::kSampleRateHz);
      rateStart = now;
      rateCount = 0;
    }
  }
}
