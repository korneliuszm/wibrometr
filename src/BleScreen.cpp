#include "BleScreen.h"
#include <cmath>
#include "UiTheme.h"

namespace {

void label(GFXcanvas16 &c, int x, int y, uint16_t color, const char *text) {
  c.setCursor(x, y);
  c.setTextColor(color);
  c.print(text);
}

const char *proximity(float d) {
  return d < 0.5f ? "BARDZO BLISKO" : d < 2 ? "BLISKO" : d < 5 ? "SREDNIO" : "DALEKO";
}

} // namespace

void drawBleScreen(GFXcanvas16 &c, const BleScreenState &s) {
  const ble::DistanceView &v = s.view;
  c.fillScreen(ui::bg);
  c.setTextSize(1);
  label(c, 0, 0, ui::white, "BLE ODLEGLOSC");
  c.setTextColor(ui::muted);
  c.setCursor(222, 0);
  c.printf("%d/%d", s.screenPos + 1, s.screenCount);

  const bool haveDistance = v.fresh && std::isfinite(v.distanceM);
  c.setCursor(0, 14);
  if (!v.scanning) {
    c.setTextColor(ui::red);
    c.print("BLE NIE DZIALA");
  } else if (haveDistance) {
    c.setTextColor(ui::green);
    c.print(proximity(v.distanceM));
  } else if (v.seen) {
    c.setTextColor(ui::yellow);
    c.printf("BRAK SYGNALU OD %lu s", (unsigned long)(v.ageMs / 1000));
  } else {
    c.setTextColor(ui::yellow);
    c.print("SZUKAM URZADZENIA...");
  }

  // Distance, large. Kept visible (greyed) for a while after the signal stops.
  c.setTextSize(4);
  c.setTextColor(haveDistance ? ui::white : ui::muted);
  c.setCursor(0, 28);
  const float d = v.distanceM;
  if (std::isfinite(d)) c.printf(d < 10 ? "%.2fm" : d < 100 ? "%.1fm" : "%.0fm", d);
  else c.print("--");
  c.setTextSize(1);

  label(c, 160, 26, ui::muted, "RSSI mediana");
  c.setTextSize(2);
  c.setTextColor(haveDistance ? ui::white : ui::muted);
  c.setCursor(160, 36);
  if (v.rssiMedian > -128) c.printf("%.0f", v.rssiMedian); else c.print("--");
  c.setTextSize(1);
  c.setCursor(160, 54);
  c.setTextColor(ui::muted);
  if (v.intervalMs) c.printf("co ~%lu ms", (unsigned long)v.intervalMs);

  // Distance bar on a log scale, 0.1 m (left) to 20 m (right).
  const int barX = 0, barY = 66, barW = 239, barH = 6;
  c.drawRect(barX, barY, barW, barH, ui::muted);
  if (haveDistance) {
    const float t = (log10f(fmaxf(0.1f, fminf(20.0f, d))) + 1.0f) / (log10f(20.0f) + 1.0f);
    c.fillRect(barX + 1, barY + 1, int(t * (barW - 2)), barH - 2, d < 2 ? ui::green : ui::yellow);
  }

  char addr[18];
  v.target.format(addr, sizeof addr);
  c.setCursor(0, 78);
  c.setTextColor(ui::white);
  c.printf("cel %s%s", addr, v.apple ? " Apple" : "");
  c.setCursor(0, 89);
  if (v.target.isResolvablePrivate() && v.seen && !v.fresh && v.ageMs > ble::kLostMs) {
    c.setTextColor(ui::yellow);
    c.print("adres prywatny: mogl sie zmienic");
  } else {
    c.setTextColor(ui::muted);
    c.printf("1 m = %.0f dBm, n = %.1f (szacunek)", v.rssiAt1m, ble::kPathLossExponent);
  }
  c.setCursor(0, 101);
  c.setTextColor(ui::muted);
  if (v.haveCandidate) {
    char other[18];
    v.candidate.addr.format(other, sizeof other);
    c.printf("najsilniejszy %s %.0f%s", other, v.candidate.rssi, v.candidate.apple ? " A" : "");
  } else {
    c.print("brak innych urzadzen");
  }
  c.setCursor(0, 112);
  c.printf("w poblizu: %u", unsigned(v.nearbyCount));

  const char *hint = "BOOT:ekran 1s:przejmij 3s:kalib.1m";
  uint16_t hintColor = ui::muted;
  if (s.buttonHeld) {
    hintColor = ui::yellow;
    hint = s.heldMs >= 3000 ? "PUSC: KALIBRACJA 1 m" : s.heldMs >= 1000 ? "PUSC: CEL = NAJSILNIEJSZY" : "PUSC: NASTEPNY EKRAN";
  } else if (s.notice) {
    hintColor = ui::yellow;
    hint = s.notice;
  }
  label(c, 0, 125, hintColor, hint);
}
