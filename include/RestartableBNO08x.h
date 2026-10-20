#pragma once
#include <Adafruit_BNO08x.h>

// BNO08x 1.2.5 SH2 close is not null-safe. With this pinned driver, _init
// always enters sh2_open, which reserves the single SHTP instance even if
// product-ID exchange fails. Release that instance only on later _init calls.
class RestartableBNO08x : public Adafruit_BNO08x {
  bool sessionStarted_ = false;
protected:
  bool _init(int32_t sensorId) override {
    if (sessionStarted_) sh2_close();
    sessionStarted_ = true;
    return Adafruit_BNO08x::_init(sensorId);
  }
};
