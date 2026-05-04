// src/ui/PromptBadge.cpp
#include "PromptBadge.h"

#include <Adafruit_ST7789.h>

namespace ui {

void drawPromptBadge(Adafruit_ST7789& tft, const char* tool) {
    // Mid-grey 1-px frame (RGB565 r=15, g=31, b=15).
    constexpr uint16_t kBorder = 0x7BEF;
    tft.drawRect(kPromptBadgeX, kPromptBadgeY,
                 kPromptBadgeW, kPromptBadgeH, kBorder);

    // Orange '?' icon at left.
    tft.setTextSize(1);
    tft.setTextColor(kPromptBadgeQColor, ST77XX_BLACK);
    tft.setCursor(kPromptBadgeX + 6, kPromptBadgeY + 5);
    tft.print('?');

    // Tool · "approve?" label.
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(kPromptBadgeX + 16, kPromptBadgeY + 5);
    tft.print((tool && tool[0]) ? tool : "?");
    tft.print(" \xB7 approve?");                      // 0xB7 ≈ middle dot

    // Press hint at right.
    tft.setTextColor(kBorder, ST77XX_BLACK);
    tft.setCursor(kPromptBadgeX + kPromptBadgeW - 50, kPromptBadgeY + 5);
    tft.print("press \x7");                           // 0x07 ≈ small bullet
}

}  // namespace ui
