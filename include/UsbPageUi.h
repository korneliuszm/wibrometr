#pragma once
#include <cstdint>

#include "TouchGestures.h"

// The last page: USB disk mode, touch calibration and clearing the card, three wide buttons.
namespace usbui {

constexpr Rect kUsbButton{30, 38, 260, 46};
constexpr Rect kCalButton{30, 116, 260, 40};
constexpr Rect kCleanButton{30, 162, 260, 40};

enum class Button : uint8_t { None, Usb, Calibrate, Clean };

constexpr Button hit(int x, int y) {
  if (kUsbButton.contains(x, y)) return Button::Usb;
  if (kCalButton.contains(x, y)) return Button::Calibrate;
  if (kCleanButton.contains(x, y)) return Button::Clean;
  return Button::None;
}

// Clearing the card asks twice: the first press arms it, the second within this time does it.
constexpr uint32_t kConfirmMs = 5000;
constexpr uint32_t kResultShownMs = 6000;  // How long the "removed N files" line stays.

}  // namespace usbui
