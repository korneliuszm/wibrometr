#pragma once
#include <Adafruit_GFX.h>
#include "ui/UiModel.h"

// Pages of the roller-line product at 320x240 (content area below the header).
namespace ui {
void drawLinePage(GFXcanvas16 &c, const Model &m);
void drawGyroPage(GFXcanvas16 &c, const Model &m);
void drawDisplacementPage(GFXcanvas16 &c, const Model &m);
void drawDisplacementPlotPage(GFXcanvas16 &c, const Model &m);
void drawDisplacementPathPage(GFXcanvas16 &c, const Model &m);
void drawWebPage(GFXcanvas16 &c, const Model &m);
} // namespace ui
