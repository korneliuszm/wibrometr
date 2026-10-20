#pragma once
#include <Adafruit_BNO08x.h>
#include <SPI.h>
#include <string.h>

// Link statistics since the last beginSpi, for serial diagnostics.
struct Bno08xLinkStats {
  uint32_t reads = 0, badHeaders = 0, writeTimeouts = 0;
  uint8_t lastBad[4] = {};
};

// BNO08x 1.2.5 SH2 close is not null-safe. With this pinned driver, _init
// always enters sh2_open, which reserves the single SHTP instance even if
// product-ID exchange fails. Release that instance only on later _init calls.
//
// The library's SPI HAL also cannot be used: it writes only while INT is low,
// resets the hub after 500 ms without INT and returns 0, which shtp_send
// retries forever. beginSpi installs a bounded HAL instead: writes pull
// PS0/WAKE low so the hub asserts INT (BNO08x datasheet, SPI host writes),
// reads never block, and every failure is reported to SH2 as an error.
class RestartableBNO08x : public Adafruit_BNO08x {
  bool sessionStarted_ = false;
  static inline SPIClass *spi_ = nullptr;
  static inline int8_t cs_ = -1, int_ = -1, wake_ = -1;
  // Full-duplex writes can clock in a hub packet; keep it for the next read.
  static inline uint8_t stash_[SH2_HAL_MAX_TRANSFER_IN];
  static inline uint16_t stashLen_ = 0;

  static SPISettings settings() { return SPISettings(1000000, MSBFIRST, SPI_MODE3); }
  static bool waitInt(uint32_t ms) {
    const uint32_t start = millis();
    while (digitalRead(int_) == HIGH) {
      if (uint32_t(millis() - start) >= ms) return false;
      delay(1);
    }
    return true;
  }
  static uint16_t packetSize(const uint8_t *header) {
    return (uint16_t(header[0]) | uint16_t(header[1]) << 8) & 0x7FFF;
  }
  static int halOpen(sh2_Hal_t *) {
    stashLen_ = 0;
    waitInt(500); // Hub boots after RST, then announces itself via INT.
    return 0;
  }
  static void halClose(sh2_Hal_t *) {}
  static int halRead(sh2_Hal_t *, uint8_t *buffer, unsigned len, uint32_t *t_us) {
    if (stashLen_) {
      const uint16_t n = stashLen_ <= len ? stashLen_ : 0;
      memcpy(buffer, stash_, n);
      stashLen_ = 0;
      *t_us = micros();
      return n;
    }
    if (digitalRead(int_) == HIGH) return 0;
    *t_us = micros();
    spi_->beginTransaction(settings());
    digitalWrite(cs_, LOW);
    for (int i = 0; i < 4; ++i) buffer[i] = spi_->transfer(0);
    const uint16_t size = packetSize(buffer);
    // A floating MISO reads as 0xFF.. and would claim 32 kB: never clock past
    // what the caller can hold, abort and let the next INT retry instead.
    if (size < 4 || size > len) {
      digitalWrite(cs_, HIGH);
      spi_->endTransaction();
      ++stats.badHeaders;
      memcpy(stats.lastBad, buffer, 4);
      return 0;
    }
    for (uint16_t i = 4; i < size; ++i) buffer[i] = spi_->transfer(0);
    digitalWrite(cs_, HIGH);
    spi_->endTransaction();
    ++stats.reads;
    return size;
  }
  static int halWrite(sh2_Hal_t *, uint8_t *buffer, unsigned len) {
    if (len > SH2_HAL_MAX_TRANSFER_OUT) return SH2_ERR_BAD_PARAM;
    if (wake_ >= 0) digitalWrite(wake_, LOW);
    const bool ready = waitInt(200);
    if (wake_ >= 0) digitalWrite(wake_, HIGH);
    if (!ready) { ++stats.writeTimeouts; return SH2_ERR_TIMEOUT; }
    uint8_t header[4] = {};
    spi_->beginTransaction(settings());
    digitalWrite(cs_, LOW);
    for (unsigned i = 0; i < len; ++i) {
      const uint8_t b = spi_->transfer(buffer[i]);
      if (i < 4) header[i] = b;
      if (!stashLen_ && i < sizeof(stash_)) stash_[i] = b;
    }
    const uint16_t size = packetSize(header);
    const bool keep = !stashLen_ && size >= 4 && size <= sizeof(stash_);
    if (keep)
      for (uint16_t i = len; i < size; ++i) stash_[i] = spi_->transfer(0);
    digitalWrite(cs_, HIGH);
    spi_->endTransaction();
    if (keep) stashLen_ = size;
    return len;
  }
  static uint32_t halTime(sh2_Hal_t *) { return micros(); }

public:
  using Adafruit_BNO08x::Adafruit_BNO08x;
  static inline Bno08xLinkStats stats;
  // wakePin drives PS0: high through reset selects SPI, low requests INT.
  bool beginSpi(uint8_t csPin, uint8_t intPin, int8_t wakePin, SPIClass *spi = &SPI) {
    spi_ = spi;
    cs_ = csPin;
    int_ = intPin;
    wake_ = wakePin;
    stats = Bno08xLinkStats{};
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    pinMode(int_, INPUT_PULLUP);
    if (wake_ >= 0) {
      pinMode(wake_, OUTPUT);
      digitalWrite(wake_, HIGH);
    }
    _HAL.open = halOpen;
    _HAL.close = halClose;
    _HAL.read = halRead;
    _HAL.write = halWrite;
    _HAL.getTimeUs = halTime;
    return _init(0);
  }

protected:
  bool _init(int32_t sensorId) override {
    if (sessionStarted_) sh2_close();
    sessionStarted_ = true;
    return Adafruit_BNO08x::_init(sensorId);
  }
};
