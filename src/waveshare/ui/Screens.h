#pragma once
#include <Adafruit_GFX.h>
#include "ui/UiModel.h"

namespace ui {

// Labels of the two bar buttons for the current page; empty text = unused.
struct ActionLabels {
  const char *a1 = "";
  const char *a2 = "";
  bool a1Enabled = true, a2Enabled = true;
  uint16_t a2Color = 0xFFFF;
};
ActionLabels actionLabels(const Model &m);

// Draws header, current page, notice line and the navigation bar.
void drawScreen(GFXcanvas16 &canvas, const Model &m);

} // namespace ui
