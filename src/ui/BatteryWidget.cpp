// src/ui/BatteryWidget.cpp
#include "BatteryWidget.h"

#include <Adafruit_ST7789.h>
#include <stdio.h>

namespace ui {

namespace {
// Geometry — tuned so the icon sits to the right of a 4-character
// percent text ("100%") with a 3-px gap, and the right edge of the
// tip lands at x = 234 (3 px clear of the 240-px screen edge).
constexpr int kBodyX     = 209;
constexpr int kBodyY     = 120;
constexpr int kBodyW     = 23;
constexpr int kBodyH     = 11;
constexpr int kTipX      = kBodyX + kBodyW;     // 232
constexpr int kTipY      = kBodyY + 3;          // 123
constexpr int kTipW      = 2;
constexpr int kTipH      = 5;

// Inner area where bars are drawn (1 px inset from the body outline).
constexpr int kInnerX    = kBodyX + 2;          // 211
constexpr int kInnerY    = kBodyY + 2;          // 122
constexpr int kInnerW    = kBodyW - 4;          // 19
constexpr int kInnerH    = kBodyH - 4;          // 7

// 4 bars × 4 px each + 3 × 1 px gaps = 19 px → fits exactly.
constexpr int kBarW      = 4;
constexpr int kBarGap    = 1;
constexpr int kBarH      = kInnerH;             // 7

// Percent text — textSize 1 (6 × 8 px per char). Right-aligned so
// the trailing '%' lands at x = kBodyX - 4 = 205. With "100%" (4
// chars × 6 = 24 px) the leftmost glyph starts at x = 181, which
// is why kBatteryEraseX is 180 (1 px breathing room).
constexpr int kTextRightX = kBodyX - 4;         // 205
constexpr int kTextY      = kBodyY + 2;         // 122 — visually centred on 11-px-tall body

uint16_t fillColor(uint8_t pct, bool charging) {
    if (charging)  return ST77XX_GREEN;
    if (pct >= 50) return ST77XX_GREEN;
    if (pct >= 25) return ST77XX_YELLOW;
    return ST77XX_RED;
}
}  // namespace

void drawBatteryWidget(Adafruit_ST7789& tft,
                       uint8_t percent,
                       bool    charging,
                       uint8_t anim_step) {
    // Body outline.
    tft.drawRect(kBodyX, kBodyY, kBodyW, kBodyH, ST77XX_WHITE);
    // Tip (filled).
    tft.fillRect(kTipX, kTipY, kTipW, kTipH, ST77XX_WHITE);

    // Decide how many bars to fill. While charging we render the
    // anim_step + 1 sweep so the user sees the icon "filling up";
    // while discharging we map percent to 0..4 bars.
    uint8_t bars;
    if (charging) {
        bars = (uint8_t)((anim_step & 0x03) + 1);   // 1, 2, 3, 4
    } else if (percent >= 75) {
        bars = 4;
    } else if (percent >= 50) {
        bars = 3;
    } else if (percent >= 25) {
        bars = 2;
    } else if (percent >= 10) {
        bars = 1;
    } else {
        bars = 0;
    }

    const uint16_t color = fillColor(percent, charging);
    for (uint8_t i = 0; i < bars; ++i) {
        int bx = kInnerX + i * (kBarW + kBarGap);
        tft.fillRect(bx, kInnerY, kBarW, kBarH, color);
    }

    // Percent label. Format "NN%" / "100%" / " 0%". Width depends on
    // digit count, so we compute the start x from the right edge.
    char buf[8];
    if (percent == 0xFF) {
        snprintf(buf, sizeof(buf), "--%%");
    } else {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)percent);
    }
    const int len = (int)strlen(buf);
    const int text_w = len * 6;                     // textSize 1
    const int text_x = kTextRightX - text_w;
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(text_x, kTextY);
    tft.print(buf);
}

}  // namespace ui
