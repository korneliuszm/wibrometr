#include "RangeTask.h"
#include <VL53L0X.h>
#include "RangeUi.h"
#include "Vl53l0xDecode.h"
#include "hal/I2cBus.h"

namespace {
constexpr uint32_t kPollMs = 10;  // A new result comes every 20-200 ms; the status read is one short transaction.
VL53L0X sensor;
}  // namespace

// Puts the sensor in its power-up state. A reset of this board does not reset the sensor: one that
// was ranging when the firmware restarted (a flash, the RESET button) and was then initialised
// on top of that came up blind (ambient 9 Mcps, only random values). The device's own soft reset
// (as in the ST driver): write 0 and wait until it reads as reset, write 1 and wait for the model id.
bool resetSensor() {
  hal::I2cBus::writeByte(vl53::kAddress, vl53::kRegSoftReset, 0x00);
  uint8_t id = 0xFF;
  for (int i = 0; i < 50; ++i) {  // Up to ~100 ms; no answer at all also means "in reset".
    delay(2);
    if (!hal::I2cBus::read(vl53::kAddress, vl53::kRegModelId, &id, 1) || id == 0) break;
  }
  hal::I2cBus::writeByte(vl53::kAddress, vl53::kRegSoftReset, 0x01);
  for (int i = 0; i < 100; ++i) {  // Up to ~200 ms.
    delay(2);
    if (hal::I2cBus::read(vl53::kAddress, vl53::kRegModelId, &id, 1) && id == vl53::kModelId) return true;
  }
  return false;
}

// Reset, then the library's initialisation (~100 ms with the bus held).
bool configureSensor(int mode) {
  const bool reset = resetSensor();
  if (!reset) Serial.println("Dalmierz: brak odpowiedzi po resecie sprzetowym, probuje mimo to");
  hal::I2cBus::Exclusive bus;
  if (!bus) return false;
  sensor.setTimeout(500);
  return sensor.init() && sensor.setMeasurementTimingBudget(rangeui::budgetUs(mode));
}

bool RangeTask::begin(RecorderCore *core, int triggerMm, int mode) {
  core_ = core;
  setSettings(triggerMm, mode);
  present_ = configureSensor(mode);  // The IMU task is not running yet: a long hold of the bus is harmless.
  if (!present_) {
    Serial.println("Dalmierz: VL53L0X nie odpowiada na 0x29");
    return false;
  }
  Serial.printf("Dalmierz: VL53L0X gotowy, tryb %d, prog %d mm\n", mode, triggerMm);
  // Core 1 like the UI loop, above its priority 1: a poll must not wait for a screen flush.
  xTaskCreatePinnedToCore(entry, "range", 4096, this, 2, nullptr, 1);
  return true;
}

void RangeTask::run() {
  RangeTrigger trigger;
  uint16_t recent[16] = {};  // Last readings, printed when a measurement starts.
  unsigned recentCount = 0;
  bool running = false;
  int appliedMode = mode_.load();
  unsigned blindReadings = 0;
  uint32_t lastRecoveryMs = 0, lastResultMs = 0;
  bool configured = true;  // False after a failed (re)initialisation: retried while armed.
  // Re-initialisation holds the bus for 100+ ms, which would cost the IMU samples of a running
  // measurement (or enough errors to reconnect and close it): only while armed.
  const auto mayReinit = [&] {
    return core_->mode() == RecorderCore::Mode::Armed && !core_->startPending();
  };
  const auto reinit = [&](const char *why) {
    Serial.printf("Dalmierz: %s, ponowna inicjalizacja\n", why);
    {
      hal::I2cBus::Exclusive bus;
      if (bus) sensor.stopContinuous();
    }
    const int wanted = mode_.load();
    configured = configureSensor(wanted);
    if (configured) appliedMode = wanted;
    else Serial.println("Dalmierz: inicjalizacja nieudana, ponowie za 5 s");
    running = false;
    ranging_ = false;
    distanceMm_ = RangeTrigger::kNoReading;
    trigger.clearWindow();
    blindReadings = 0;
    lastRecoveryMs = millis();
  };
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPollMs));
    // Ranged all the time except while USB owns the card: the readings cost the IMU nothing (measured:
    // 512 samples/s, none lost), and the distance stays live on the screen and in the log.
    const bool want = core_->mode() != RecorderCore::Mode::Usb;
    const int mode = mode_.load();
    if (running && (!want || mode != appliedMode)) {
      {
        hal::I2cBus::Exclusive bus;
        if (bus) sensor.stopContinuous();
      }
      running = false;
      ranging_ = false;
      distanceMm_ = RangeTrigger::kNoReading;
      trigger.clearWindow();
    }
    if (!want) continue;
    if (!configured) {
      if (mayReinit() && millis() - lastRecoveryMs > 5000) reinit("poprzednia inicjalizacja nieudana");
      continue;
    }
    if (!running) {
      hal::I2cBus::Exclusive bus;
      if (!bus) continue;
      if (mode != appliedMode) {
        sensor.setMeasurementTimingBudget(rangeui::budgetUs(mode));
        appliedMode = mode;
      }
      sensor.startContinuous();
      running = true;
      ranging_ = true;
      lastResultMs = millis();
      continue;
    }
    // No result for 3 s (the slowest mode gives one every 200 ms): the sensor stopped answering.
    if (millis() - lastResultMs > 3000 && mayReinit() && millis() - lastRecoveryMs > 5000) {
      reinit("brak wynikow od 3 s");
      continue;
    }
    uint8_t status = 0;
    if (!hal::I2cBus::read(vl53::kAddress, vl53::kRegInterruptStatus, &status, 1)) {
      countI2cError();
      continue;
    }
    if (!vl53::resultReady(status)) continue;
    uint8_t block[vl53::kResultBytes];
    if (!hal::I2cBus::read(vl53::kAddress, vl53::kRegResult, block, sizeof block)) {
      countI2cError();
      continue;
    }
    hal::I2cBus::writeByte(vl53::kAddress, vl53::kRegInterruptClear, 0x01);
    lastResultMs = millis();
    const uint16_t mm = vl53::decodeRange(block);
    distanceMm_ = mm;
    // A sensor that reads a very high ambient rate for seconds is blind (see resetSensor): start over,
    // at most every 30 s (in direct sunlight the rate is high for real and a restart changes nothing).
    blindReadings = vl53::ambientMcps(block) > vl53::kBlindAmbientMcps ? blindReadings + 1 : 0;
    if (blindReadings >= 100 && millis() - lastRecoveryMs > 30000 && mayReinit()) {
      reinit("swiatlo otoczenia ponad 4 Mcps");
      continue;
    }
    if (debug_.load())  // Fields as in the ST API: status, effective SPADs (8.8), signal and ambient rate (9.7 Mcps).
      Serial.printf("R st=%u spad=%.1f sig=%.2f amb=%.2f mm=%u -> %u\n", unsigned((block[0] >> 3) & 0x0F),
                    ((block[2] << 8) | block[3]) / 256.0f, ((block[6] << 8) | block[7]) / 128.0f,
                    ((block[8] << 8) | block[9]) / 128.0f, unsigned((block[10] << 8) | block[11]), unsigned(mm));
    recent[recentCount++ % 16] = mm;
    portENTER_CRITICAL(&mux_);
    ++stats_.readings;
    if (mm == RangeTrigger::kNoReading) {
      ++stats_.noEcho;
    } else {
      if (mm < stats_.minMm) stats_.minMm = mm;
      if (mm > stats_.maxMm) stats_.maxMm = mm;
    }
    portEXIT_CRITICAL(&mux_);
    if (trigger.update(mm, uint16_t(triggerMm_.load())) == RangeTrigger::Event::Start) {
      Serial.printf("Dalmierz: wyzwolenie, prog %d mm, ostatnie odczyty (65535 = brak echa):", triggerMm_.load());
      for (unsigned i = recentCount > 16 ? recentCount - 16 : 0; i < recentCount; ++i) Serial.printf(" %u", recent[i % 16]);
      Serial.println();
      core_->requestStart(triggerMm_.load());  // Dropped by the core while it is recording.
    }
  }
}
