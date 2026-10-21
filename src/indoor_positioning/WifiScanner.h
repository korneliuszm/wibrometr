#pragma once
#include "WifiFingerprint.h"

// Non-blocking Wi-Fi scanning on top of the Arduino WiFi library: start()
// returns at once, poll() is called from the main loop until the scan ends.
// The radio is put in station mode without connecting anywhere; nothing in
// this firmware uses a Wi-Fi connection, so scanning disturbs no link.
namespace ips {

class WifiScanner {
public:
  enum class State { Idle, Running, Done, Failed };
  void begin();
  bool start();                 // false if a scan could not be started.
  State poll(ScanResult &out);  // Done/Failed are reported once, then Idle.
  bool busy() const { return running_; }

private:
  bool started_ = false, running_ = false;
  uint32_t startMs_ = 0;
};

} // namespace ips
