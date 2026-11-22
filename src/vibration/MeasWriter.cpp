#include "MeasWriter.h"
#include <SD.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <cstdio>
#include <cstring>
#include "VibConfig.h"
#include "hal/BoardPins.h"
#include "hal/Display.h"

namespace {
enum : uint8_t { kOpen = 1, kSample = 2, kClose = 3 };
constexpr size_t kBufferTarget = 8192;    // Write to the card in chunks of about this size.
constexpr uint32_t kWriteEveryMs = 250;   // ...or at least this often.
constexpr uint32_t kFlushEveryMs = 2000;  // File system flush: bounds data lost on power loss.
constexpr uint32_t kMountRetryMs = 1000;
constexpr int kGlitchRetries = 3;          // Remount attempts before a write error is final.
constexpr int kRecordsPerPass = 256;
// One queue keeps open/samples/close in order. Samples may not take the last
// kControlReserve places, so open() and close() still fit when samples overflow.
// 8192 samples at 2 kHz: the 1 s pre-roll replay (~2000 samples in one burst)
// plus ~3 s of card stall. 24 B each (~200 KB), so the queue lives in PSRAM.
constexpr UBaseType_t kSampleCapacity = 8192;
constexpr UBaseType_t kControlReserve = 16;
constexpr UBaseType_t kQueueLength = kSampleCapacity + kControlReserve;
} // namespace

void MeasWriter::begin(float sampleRateHz, float rangeG) {
  sampleRateHz_ = sampleRateHz;
  rangeG_ = rangeG;
  setState(State::NoCard, "brak karty");
  queue_ = xQueueCreateWithCaps(kQueueLength, sizeof(Item), MALLOC_CAP_SPIRAM);
  if (!queue_) queue_ = xQueueCreate(kSampleCapacity / 8 + kControlReserve, sizeof(Item));  // No PSRAM.
  xTaskCreatePinnedToCore(taskEntry, "measwr", 8192, this, 1, nullptr, 0);
}

// ---- Sensor task side: never blocks ------------------------------------------

bool MeasWriter::open(int thresholdDeci) {
  if (!queue_ || parkWanted_.load() || !accepting_.load()) return false;
  const Item it{kOpen, thresholdDeci, 0, 0, 0, 0};
  if (xQueueSend(queue_, &it, 0) != pdTRUE) return false;
  fileDropped_ = 0;
  fileMissed_ = 0;
  return true;
}

void MeasWriter::write(int32_t relUs, const Accel &a) {
  if (!queue_) return;
  const Item it{kSample, relUs, 0, a.x, a.y, a.z};
  if (uxQueueSpacesAvailable(queue_) <= kControlReserve || xQueueSend(queue_, &it, 0) != pdTRUE) {
    ++dropped_;
    ++fileDropped_;
  }
}

void MeasWriter::close() {
  if (!queue_) return;
  const Item it{kClose, int32_t(fileDropped_.load()), fileMissed_.load(), 0, 0, 0};
  // Practically never fails (reserved places); if it does, the file is closed by
  // the next open() or park().
  if (xQueueSend(queue_, &it, 0) != pdTRUE) ++dropped_;
}

void MeasWriter::park() { parkWanted_ = true; }
void MeasWriter::unpark() { parkWanted_ = false; }

MeasStatus MeasWriter::status() const {
  MeasStatus s;
  uint32_t started;
  portENTER_CRITICAL(&mux_);
  s = status_;
  started = startedMs_;
  portEXIT_CRITICAL(&mux_);
  s.dropped = dropped_.load();
  s.recordingMs = s.state == State::Recording ? millis() - started : 0;
  return s;
}

// ---- Writer task --------------------------------------------------------------

void MeasWriter::setState(State state, const char *message) {
  state_ = state;
  accepting_ = (state == State::Idle || state == State::Recording) && !full_;
  portENTER_CRITICAL(&mux_);
  status_.state = state;
  std::strncpy(status_.message, message, sizeof status_.message - 1);
  status_.message[sizeof status_.message - 1] = '\0';
  portEXIT_CRITICAL(&mux_);
}

void MeasWriter::publishCounters() {
  portENTER_CRITICAL(&mux_);
  status_.number = number_;
  status_.samples = samples_;
  status_.fileBytes = fileBytes_;
  status_.measurements = measurements_;
  status_.recoveries = recoveries_;
  portEXIT_CRITICAL(&mux_);
}

void MeasWriter::fail(const char *message) {
  if (file_) file_.close();
  used_ = 0;
  Serial.printf("SD: %s\n", message);
  setState(State::Error, message);
  nextMountMs_ = millis() + kMountRetryMs;
}

bool MeasWriter::mount() {
  if (file_) file_.close();  // Only after a failure; the handle is stale anyway.
  used_ = 0;
  full_ = false;
  SD.end();
  if (!SD.begin(board::kSdCs, hal::spiBus(), board::kSdSpiHz) || SD.cardType() == CARD_NONE) {
    setState(State::NoCard, "brak karty");
    return false;
  }
  if (!SD.exists(measfmt::kDirectory) && !SD.mkdir(measfmt::kDirectory)) {
    setState(State::Error, "nie mozna utworzyc /MEAS");
    return false;
  }
  highest_ = 0;
  measurements_ = 0;
  File dir = SD.open(measfmt::kDirectory);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    setState(State::Error, "/MEAS nie jest katalogiem");
    return false;
  }
  // File names only: much faster than opening every file.
  for (;;) {
    bool isDir = false;
    const String name = dir.getNextFileName(&isDir);
    if (name.length() == 0) break;
    unsigned n = 0;
    if (!isDir && measfmt::parseMeasNumber(name.c_str(), n)) {
      ++measurements_;
      if (n > highest_) highest_ = n;
    }
  }
  dir.close();
  const uint32_t cardMb = uint32_t(SD.cardSize() / (1024ULL * 1024ULL));
  portENTER_CRITICAL(&mux_);
  status_.cardMb = cardMb;
  portEXIT_CRITICAL(&mux_);
  number_ = highest_;
  samples_ = fileBytes_ = 0;
  publishCounters();
  Serial.printf("SD: card %lu MB, %lu measurements, newest M%04u\n", (unsigned long)cardMb,
                (unsigned long)measurements_, highest_);
  if (measfmt::nextMeasNumber(highest_) == 0) {
    full_ = true;
    setState(State::Error, "numeracja pelna");
    return false;
  }
  setState(State::Idle, "gotowa");
  return true;
}

void MeasWriter::startFile(int thresholdDeci) {
  if (state_ != State::Idle) return;  // Card not ready: the samples are discarded.
  unsigned number = measfmt::nextMeasNumber(highest_);
  char path[32];
  // Never overwrite: skip numbers whose file exists (a file the scan missed).
  for (; number != 0; number = measfmt::nextMeasNumber(number)) {
    measfmt::measPath(number, path, sizeof path);
    if (!SD.exists(path)) break;
    highest_ = number;
  }
  if (number == 0) {
    full_ = true;
    setState(State::Error, "numeracja pelna");
    return;
  }
  file_ = SD.open(path, FILE_WRITE);
  if (!file_) {
    // A card that just finished a long write (or took a knock) can refuse one open:
    // remount once before giving up, so the trigger is not lost.
    Serial.printf("SD: open %s failed, remounting\n", path);
    if (remountCard()) file_ = SD.open(path, FILE_WRITE);
    if (!file_) return fail("nie mozna utworzyc pliku");
    ++recoveries_;
  }
  measfmt::Header h;
  h.number = number;
  h.thresholdDeci = thresholdDeci;
  const float measured = measuredHz_.load();
  h.sampleRateHz = measured > 0 ? measured : sampleRateHz_;  // Measured from the FIFO once known.
  h.rangeG = rangeG_;
  h.preRollS = float(vib::kPreRollUs) / 1e6f;
  h.quietS = float(vib::kQuietUs) / 1e6f;
  used_ = measfmt::formatHeader(buffer_, sizeof buffer_, h);
  highest_ = number_ = number;
  ++measurements_;
  samples_ = fileBytes_ = 0;
  const uint32_t now = millis();
  lastWriteMs_ = lastFlushMs_ = now;
  portENTER_CRITICAL(&mux_);
  startedMs_ = now;
  portEXIT_CRITICAL(&mux_);
  // Header on the card at once: a reset or power cut in the first seconds then
  // leaves a file that says what it is, not an empty one. The queue absorbs
  // the samples meanwhile.
  if (!flushBuffer()) return;  // fail() reported it.
  file_.flush();
  publishCounters();
  setState(State::Recording, "nagrywanie");
  Serial.printf("SD: recording %s (threshold %d.%d m/s2)\n", path, thresholdDeci / 10, thresholdDeci % 10);
}

// SD.end + SD.begin on the shared bus, without touching numbering or state.
bool MeasWriter::remountCard() {
  SD.end();
  vTaskDelay(pdMS_TO_TICKS(30));
  return SD.begin(board::kSdCs, hal::spiBus(), board::kSdSpiHz) && SD.cardType() != CARD_NONE;
}

// A write failed mid-measurement (shock on the TF socket, card busy too long):
// remount and reopen the same file for append, so one glitch does not end the
// measurement. Data FatFs had cached since the last flush may be gone; a comment
// line marks the place (the t_ms column shows the gap).
bool MeasWriter::reopenAfterGlitch() {
  if (file_) file_.close();
  char path[32];
  measfmt::measPath(number_, path, sizeof path);
  for (int attempt = 0; attempt < kGlitchRetries; ++attempt) {
    if (attempt) vTaskDelay(pdMS_TO_TICKS(100 * attempt));
    if (!remountCard()) continue;
    file_ = SD.open(path, FILE_APPEND);
    if (!file_) continue;
    ++recoveries_;
    file_.print("# card glitch: remounted, samples since the last flush may be missing\n");
    lastFlushMs_ = millis();
    Serial.printf("SD: recovered %s (attempt %d)\n", path, attempt + 1);
    return true;
  }
  return false;
}

bool MeasWriter::flushBuffer() {
  if (!file_) return false;
  if (used_ == 0) return true;
  size_t done = 0;
  for (int tries = 0;; ++tries) {
    done += file_.write(reinterpret_cast<const uint8_t *>(buffer_) + done, used_ - done);
    if (done >= used_) break;
    Serial.printf("SD: short write %u/%u, recovering\n", unsigned(done), unsigned(used_));
    if (tries >= kGlitchRetries || !reopenAfterGlitch()) {
      fail("blad zapisu na karte");
      return false;
    }
  }
  fileBytes_ += uint32_t(used_);
  used_ = 0;
  lastWriteMs_ = millis();
  return true;
}

void MeasWriter::finishFile(uint32_t dropped, uint32_t missed) {
  if (!file_) return;
  if (used_ + 128 > sizeof buffer_ && !flushBuffer()) return;
  if (missed) {  // The sensor was read too late: these samples never existed here (gaps in t_ms).
    used_ += size_t(std::snprintf(buffer_ + used_, sizeof buffer_ - used_, "# missed_samples: %lu\n",
                                  (unsigned long)missed));
    if (used_ + 128 > sizeof buffer_ && !flushBuffer()) return;
  }
  used_ += measfmt::formatFooter(buffer_ + used_, sizeof buffer_ - used_, samples_, dropped);
  if (!flushBuffer()) return;  // fail() closed the file.
  file_.flush();
  file_.close();
  publishCounters();
  Serial.printf("SD: closed M%04u, %lu samples, %lu bytes, %lu dropped\n", number_, (unsigned long)samples_,
                (unsigned long)fileBytes_, (unsigned long)dropped);
  if (measfmt::nextMeasNumber(highest_) == 0) {
    full_ = true;
    setState(State::Error, "numeracja pelna");
  } else {
    setState(State::Idle, "gotowa");
  }
}

void MeasWriter::process(const Item &it) {
  switch (it.kind) {
  case kOpen:
    if (file_) finishFile(fileDropped_.load());  // Its close() was lost.
    startFile(it.value);
    break;
  case kSample:
    if (!file_) return;  // Not recording (card not ready, or after an error).
    if (used_ + 128 > sizeof buffer_ && !flushBuffer()) return;
    used_ += measfmt::formatSample(buffer_ + used_, sizeof buffer_ - used_, it.value, it.x, it.y, it.z);
    ++samples_;
    break;
  case kClose:
    finishFile(uint32_t(it.value), it.extra);
    break;
  default:
    break;
  }
}

void MeasWriter::doPark() {
  // Everything queued before park() is written (normally the RecorderCore has
  // already queued close()); bounded, in case samples keep coming.
  Item it;
  for (UBaseType_t i = 0; i < kQueueLength && xQueueReceive(queue_, &it, 0) == pdTRUE; ++i) process(it);
  if (file_) finishFile(fileDropped_.load());
  if (file_) file_.close();
  used_ = 0;
  isParked_ = true;
  setState(State::Parked, "karta u komputera");
  Serial.println("SD: parked, card handed over");
}

void MeasWriter::doUnpark() {
  isParked_ = false;
  // The host may have rewritten any sector: drop the FatFs state and remount.
  SD.end();
  mount();
  nextMountMs_ = millis() + kMountRetryMs;
  Serial.println("SD: unparked");
}

void MeasWriter::run() {
  mount();
  nextMountMs_ = millis() + kMountRetryMs;
  for (;;) {
    const bool wantPark = parkWanted_.load();
    if (wantPark && !isParked_.load()) doPark();
    else if (!wantPark && isParked_.load()) doUnpark();
    if (isParked_.load()) {
      // No card access at all; anything queued meanwhile is discarded.
      Item discard;
      while (xQueueReceive(queue_, &discard, 0) == pdTRUE) {}
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    Item it;
    for (int i = 0; i < kRecordsPerPass && xQueueReceive(queue_, &it, 0) == pdTRUE; ++i) process(it);
    const uint32_t now = millis();
    if (file_) {
      if (used_ >= kBufferTarget || (used_ && now - lastWriteMs_ >= kWriteEveryMs)) flushBuffer();
      if (file_ && now - lastFlushMs_ >= kFlushEveryMs) {
        file_.flush();
        lastFlushMs_ = now;
      }
      publishCounters();
      vTaskDelay(pdMS_TO_TICKS(5));
    } else {
      if ((state_ == State::NoCard || state_ == State::Error) && !full_ && int32_t(now - nextMountMs_) >= 0) {
        mount();
        nextMountMs_ = now + kMountRetryMs;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}
