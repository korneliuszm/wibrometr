#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>
#include "ble_ranging/BleDistance.h"

// "BLE odleglosc" screen: estimated distance to the target device, its RSSI,
// address and the strongest other device (for adopting a new target after an
// address rotation). Drawing only, from a snapshot; renders off-device too.
struct BleScreenState {
  ble::DistanceView view;
  int screenPos = 0, screenCount = 1;
  bool buttonHeld = false;
  uint32_t heldMs = 0;
  const char *notice = nullptr; // Active notice, if any.
};

void drawBleScreen(GFXcanvas16 &canvas, const BleScreenState &s);
