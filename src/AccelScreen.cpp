#include "AccelScreen.h"
#include <cmath>
#include "Mpu6050Decode.h"
#include "UiTheme.h"

namespace {
constexpr float fullScale = 20.0f; // Bar end: +-20 m/s2 (a little over 2 g).
constexpr int rowY[3] = {13, 45, 77};
constexpr int barMid = 120, barHalf = 118;
const uint16_t axisColor[3] = {ui::red, ui::green, ui::blue};
const char axisLetter[3] = {'X', 'Y', 'Z'};

float component(const Accel &a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }
} // namespace

void drawAccelScreen(GFXcanvas16 &c, const AccelScreenState &s) {
  c.fillScreen(ui::bg);
  c.setTextSize(1);
  c.setCursor(0, 0);
  c.setTextColor(ui::white);
  c.print("PRZYSPIESZENIE");
  c.setCursor(162, 0);
  c.printf("%3.0f Hz", s.live ? s.rateHz : 0);
  c.setTextColor(ui::muted);
  c.setCursor(222, 0);
  c.printf("%d/%d", s.screenPos + 1, s.screenCount);

  if (!s.live) {
    // No data: say what the bus looks like instead of three rows of dashes.
    c.setCursor(0, 14);
    c.setTextColor(ui::yellow);
    c.print(s.status);
    c.setCursor(0, 28);
    c.setTextColor(ui::white);
    c.print(s.error[0] ? s.error : "czekam na pierwsza probe...");
    c.setCursor(0, 46);
    c.setTextColor(ui::muted);
    c.printf("I2C SDA=%d SCL=%d %lu kHz, odpowiada:", s.sda, s.scl, (unsigned long)(s.i2cHz / 1000));
    c.setTextSize(2);
    c.setCursor(0, 58);
    c.setTextColor(ui::white);
    c.print(s.scan[0] ? s.scan : "-");
    c.setTextSize(1);
    c.setTextColor(ui::muted);
    c.setCursor(0, 82);
    c.print("oczekiwane: 0x68 (AD0=GND) lub 0x69");
    c.setCursor(0, 93);
    c.print("0x36 = uklad akumulatora na plytce");
    c.setCursor(0, 107);
    c.print("VCC-3V GND-GND SDA-GPIO42 SCL-GPIO41");
    return;
  }
  for (int i = 0; i < 3; ++i) {
    const float v = component(s.value, i);
    const int y = rowY[i];
    c.setTextSize(3);
    c.setTextColor(axisColor[i]);
    c.setCursor(0, y);
    c.printf("%c", axisLetter[i]);
    c.setTextColor(ui::white);
    c.setCursor(22, y);
    c.printf("%+6.2f", v);
    c.setTextSize(1);
    c.setCursor(136, y + 3);
    c.setTextColor(ui::muted);
    c.print("m/s2");
    c.setCursor(136, y + 14);
    c.setTextColor(ui::white);
    c.printf("%+.2f g", v / mpu6050::kGravity);
    // Bar: centre = 0; clipped at the ends.
    const int by = y + 26;
    c.drawFastHLine(0, by + 2, 240, ui::grid);
    c.drawFastVLine(barMid, by - 1, 7, ui::muted);
    const int len = int(lroundf(fmaxf(-1.0f, fminf(1.0f, v / fullScale)) * barHalf));
    if (len >= 0) c.fillRect(barMid, by, len + 1, 5, axisColor[i]);
    else c.fillRect(barMid + len, by, -len, 5, axisColor[i]);
  }
  c.setCursor(0, 111);
  const Accel &a = s.value;
  const float mag = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  c.setTextColor(ui::muted);
  c.printf("|a| %.2f m/s2  %.2f g", mag, mag / mpu6050::kGravity);
  if (s.tared) {
    c.setCursor(186, 111);
    c.setTextColor(ui::yellow);
    c.print("TARA");
  }
}
