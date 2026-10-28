#pragma once
#include "LineTracker.h"

// Wi-Fi fingerprint for the line tracker: a few asynchronous scans (the
// board's own access point keeps running; its clients pause while the radio
// visits other channels), averaged per access point. An access point missed
// by a scan counts as line::missingDbm for that scan. Loop task only.
namespace wifiscan {
void start();
void poll();
bool busy();
// True once per finished fingerprint.
bool take(line::Fingerprint &out);
// Network name last seen for an access point, or "" if not known.
const char *ssidOf(const uint8_t *bssid);
} // namespace wifiscan
