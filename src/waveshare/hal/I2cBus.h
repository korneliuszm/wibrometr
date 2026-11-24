#pragma once
#include <cstddef>
#include <cstdint>

// The touch controller and the IMU share one I2C bus and are used from
// different tasks, so every transaction goes through these functions, which
// hold a mutex for its duration.
namespace hal {

class I2cBus {
public:
  static void begin();                  // Creates the mutex, starts Wire.
  static void recover();                // Restarts Wire; frees a bus left stuck by a glitch.
  static bool probe(uint8_t address);   // True if a device acknowledges.
  static bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t length);
  static bool write(uint8_t address, uint8_t reg, const uint8_t *data, size_t length);
  // Holds the bus for a code block that talks to Wire directly (a library that
  // does not know this class), e.g. the VL53L0X driver. Keep it short: the IMU is
  // polled every millisecond. `if (!lock) return;` after a 50 ms timeout.
  class Exclusive {
  public:
    Exclusive();
    ~Exclusive();
    Exclusive(const Exclusive &) = delete;
    Exclusive &operator=(const Exclusive &) = delete;
    explicit operator bool() const { return held_; }
  private:
    bool held_;
  };
  static bool writeByte(uint8_t address, uint8_t reg, uint8_t value) { return write(address, reg, &value, 1); }
};

} // namespace hal
