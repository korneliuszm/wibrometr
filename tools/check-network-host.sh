#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
for suite in wifi_config network_files network_json csv_log station_config motion_classifier distance_log line displacement; do
  g++ -std=c++17 -O2 -Iinclude -Isrc "test/${suite}_checks.cpp" -o "/tmp/wibrometr_${suite}_checks"
  "/tmp/wibrometr_${suite}_checks"
done
g++ -std=c++17 -Itest/fakes -Isrc -Iinclude test/wifi_scan_checks.cpp src/WifiScan.cpp -o /tmp/wibrometr_wifi_scan_checks
/tmp/wibrometr_wifi_scan_checks
g++ -std=c++17 -Iinclude -fsyntax-only test/signal_checks.cpp
if command -v node >/dev/null; then
  node test/arr_changed.mjs
  node test/scene_geometry.mjs
else
  ELECTRON_RUN_AS_NODE=1 /snap/code/current/usr/share/code/code test/arr_changed.mjs
  ELECTRON_RUN_AS_NODE=1 /snap/code/current/usr/share/code/code test/scene_geometry.mjs
fi
