#include "hal/SdLogger.h"
#include <FS.h>
#include <SD.h>
#include <cstring>
#include "CsvLog.h"
#include "hal/BoardPins.h"
#include "hal/Display.h"

namespace hal {

namespace {
enum : uint8_t { kSample = 0, kBattery = 1 };
enum : uint8_t { kStart = 1, kStop = 2, kRideRow = 3 };
constexpr size_t kBufferTarget = 3584;       // Write to the card in chunks of about this size.
constexpr uint32_t kWriteEveryMs = 250;      // ...or at least this often.
constexpr uint32_t kFlushEveryMs = 2000;     // File system flush: bounds data lost on power loss.
constexpr uint32_t kMountRetryMs = 3000;
constexpr int kRecordsPerPass = 256;
} // namespace

void SdLogger::begin() {
  records_ = xQueueCreate(1024, sizeof(Record)); // ~2 s of samples at 500 Hz.
  commands_ = xQueueCreate(8, sizeof(Command));
  xTaskCreatePinnedToCore(taskEntry, "sdlog", 8192, this, 1, nullptr, 0);
}

void SdLogger::start(const char *tag, float sampleRateHz, float rangeG) {
  Command c{};
  c.kind = kStart;
  c.sampleRateHz = sampleRateHz;
  c.rangeG = rangeG;
  std::strncpy(c.tag, tag ? tag : "", logfmt::kMaxTagLen);
  if (commands_) xQueueSend(commands_, &c, 0);
}

void SdLogger::stop() {
  Command c{};
  c.kind = kStop;
  if (commands_) xQueueSend(commands_, &c, 0);
}

void SdLogger::appendRideRow(const char *file, const char *csvRow) {
  if (!commands_ || !csvRow || !file) return;
  Command c{};
  c.kind = kRideRow;
  std::strncpy(c.file, file, sizeof c.file - 1);
  std::strncpy(c.row, csvRow, sizeof c.row - 1);
  xQueueSend(commands_, &c, 0);
}

void SdLogger::pushSample(uint32_t tUs, const Accel &a) {
  if (!recording_.load() || !records_) return;
  const Record r{kSample, tUs, a.x, a.y, a.z};
  if (xQueueSend(records_, &r, 0) != pdTRUE) ++dropped_;
}

void SdLogger::pushBattery(uint32_t tUs, float millivolts, bool charging) {
  if (!recording_.load() || !records_) return;
  const Record r{kBattery, tUs, millivolts, charging ? 1.0f : 0.0f, 0};
  xQueueSend(records_, &r, 0); // An event may be lost under overload; samples matter more.
}

SdLogger::Status SdLogger::status() const {
  Status s;
  portENTER_CRITICAL(&mux_);
  s = status_;
  portEXIT_CRITICAL(&mux_);
  s.dropped = dropped_.load();
  if (s.state == State::Recording) s.recordingMs = millis() - startedMs_;
  return s;
}

void SdLogger::setState(State state, const char *message) {
  portENTER_CRITICAL(&mux_);
  status_.state = state;
  std::strncpy(status_.message, message, sizeof status_.message - 1);
  status_.message[sizeof status_.message - 1] = '\0';
  portEXIT_CRITICAL(&mux_);
}

void SdLogger::fail(const char *message) {
  recording_ = false;
  if (file_) {
    static_cast<File *>(file_)->close();
    delete static_cast<File *>(file_);
    file_ = nullptr;
  }
  used_ = 0;
  Serial.printf("SD: %s\n", message);
  setState(State::Error, message);
  nextMountMs_ = millis() + kMountRetryMs;
}

bool SdLogger::mount() {
  SD.end();
  if (!SD.begin(board::kSdCs, spiBus(), board::kSdSpiHz) || SD.cardType() == CARD_NONE) {
    setState(State::NoCard, "brak karty");
    return false;
  }
  if (!SD.exists(logfmt::kDirectory) && !SD.mkdir(logfmt::kDirectory)) {
    setState(State::Error, "nie mozna utworzyc /LOGS");
    return false;
  }
  highestSession_ = 0;
  File dir = SD.open(logfmt::kDirectory);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      unsigned n = 0;
      if (logfmt::parseSessionNumber(f.name(), n) && n > highestSession_) highestSession_ = n;
      f.close();
    }
    dir.close();
  }
  portENTER_CRITICAL(&mux_);
  status_.cardMb = uint32_t(SD.cardSize() / (1024ULL * 1024ULL));
  portEXIT_CRITICAL(&mux_);
  Serial.printf("SD: card %lu MB, newest session S%04u\n", (unsigned long)status_.cardMb, highestSession_);
  setState(State::Idle, "gotowa");
  return true;
}

void SdLogger::beginSession(const Command &c) {
  if (status_.state != State::Idle) {
    Serial.println("SD: start ignored, card not ready");
    return;
  }
  const unsigned session = logfmt::nextSession(highestSession_);
  char path[32];
  logfmt::sessionPath(session, path, sizeof path);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return fail("nie mozna utworzyc pliku");
  file_ = new File(f);
  logfmt::Header header;
  header.session = session;
  header.uptimeMs = millis();
  header.sampleRateHz = c.sampleRateHz;
  header.rangeG = c.rangeG;
  logfmt::setTag(header, c.tag);
  used_ = logfmt::formatHeader(buffer_, sizeof buffer_, header);
  highestSession_ = session;
  startedMs_ = lastWriteMs_ = lastFlushMs_ = millis();
  dropped_ = 0;
  portENTER_CRITICAL(&mux_);
  status_.session = session;
  status_.fileBytes = 0;
  status_.samples = 0;
  portEXIT_CRITICAL(&mux_);
  recording_ = true;
  setState(State::Recording, "nagrywanie");
  Serial.printf("SD: recording %s (tag '%s')\n", path, header.tag);
}

void SdLogger::flushBuffer() {
  if (!file_ || used_ == 0) return;
  File &f = *static_cast<File *>(file_);
  const size_t written = f.write(reinterpret_cast<const uint8_t *>(buffer_), used_);
  if (written != used_) return fail("blad zapisu na karte");
  portENTER_CRITICAL(&mux_);
  status_.fileBytes += uint32_t(used_);
  portEXIT_CRITICAL(&mux_);
  used_ = 0;
  lastWriteMs_ = millis();
}

void SdLogger::endSession() {
  recording_ = false;
  if (!file_) return;
  // Everything the sensor task queued before stop() is still written.
  Record r;
  while (xQueueReceive(records_, &r, 0) == pdTRUE) {
    if (used_ + 128 > sizeof buffer_) flushBuffer();
    if (!file_) return;
    used_ += r.kind == kSample ? logfmt::formatSample(buffer_ + used_, sizeof buffer_ - used_, r.tUs, r.a, r.b, r.c)
                               : logfmt::formatBatteryEvent(buffer_ + used_, sizeof buffer_ - used_, r.tUs, r.a, r.b > 0.5f);
  }
  flushBuffer();
  if (!file_) return;
  File *f = static_cast<File *>(file_);
  f->flush();
  f->close();
  delete f;
  file_ = nullptr;
  Serial.printf("SD: closed S%04u, %lu bytes\n", status_.session, (unsigned long)status_.fileBytes);
  setState(State::Idle, "gotowa");
}

void SdLogger::handle(const Command &c) {
  if (c.kind == kRideRow) {
    if (status_.state != State::Idle && status_.state != State::Recording) return;
    char path[64];
    // "/sd" is the Arduino SD library's VFS mount point.
    std::snprintf(path, sizeof path, "/sd%s/distances_last_run.csv", logfmt::kDirectory);
    storage::appendCsvFile(path, c.row);
    if (c.file[0]) {
      std::snprintf(path, sizeof path, "/sd%s/%s.csv", logfmt::kDirectory, c.file);
      storage::appendCsvFile(path, c.row);
    }
    return;
  }
  if (c.kind == kStart) beginSession(c);
  else if (c.kind == kStop && status_.state == State::Recording) endSession();
}

void SdLogger::run() {
  mount();
  nextMountMs_ = millis() + kMountRetryMs;
  for (;;) {
    Command c;
    while (xQueueReceive(commands_, &c, 0) == pdTRUE) handle(c);
    const uint32_t now = millis();
    if (status_.state == State::Recording) {
      Record r;
      for (int i = 0; i < kRecordsPerPass && xQueueReceive(records_, &r, 0) == pdTRUE; ++i) {
        if (used_ + 128 > sizeof buffer_) flushBuffer();
        if (!file_) break;
        const size_t n = r.kind == kSample
                             ? logfmt::formatSample(buffer_ + used_, sizeof buffer_ - used_, r.tUs, r.a, r.b, r.c)
                             : logfmt::formatBatteryEvent(buffer_ + used_, sizeof buffer_ - used_, r.tUs, r.a, r.b > 0.5f);
        used_ += n;
        if (r.kind == kSample) {
          portENTER_CRITICAL(&mux_);
          ++status_.samples;
          portEXIT_CRITICAL(&mux_);
        }
      }
      if (file_ && (used_ >= kBufferTarget || (used_ && now - lastWriteMs_ >= kWriteEveryMs))) flushBuffer();
      if (file_ && now - lastFlushMs_ >= kFlushEveryMs) {
        static_cast<File *>(file_)->flush();
        lastFlushMs_ = now;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    } else {
      Record discard;
      while (xQueueReceive(records_, &discard, 0) == pdTRUE) {}
      if (status_.state != State::Idle && int32_t(now - nextMountMs_) >= 0) {
        mount();
        nextMountMs_ = now + kMountRetryMs;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

} // namespace hal
