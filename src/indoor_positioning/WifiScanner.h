#pragma once
#include "WifiFingerprint.h"

// Non-blocking Wi-Fi scanning on the ESP-IDF Wi-Fi driver: start() returns at
// once, poll() is called from the main loop until the scan ends. The radio
// runs in station mode without connecting anywhere and keeps its settings in
// RAM only. ESP-IDF is used directly rather than the Arduino WiFi library:
// that library drags in the Network/lwIP socket sources, which fail to build
// on Windows once pioarduino shortens the include paths.
namespace ips {

class WifiScanner {
public:
  enum class State { Idle, Running, Done, Failed };
  bool begin();                 // Brings the radio up once; false on driver error.
  bool start();                 // false if a scan could not be started.
  State poll(ScanResult &out);  // Done/Failed are reported once, then Idle.
  bool busy() const { return running_; }

private:
  bool started_ = false, running_ = false;
  uint32_t startMs_ = 0;
};

} // namespace ips
