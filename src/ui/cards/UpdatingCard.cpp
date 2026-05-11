#include "UpdatingCard.h"

#include "../../core/UpdateManager.h"
#include "../../display/Display.h"

#include <Adafruit_ST7789.h>

namespace {
constexpr uint32_t kMinRedrawIntervalMs = 250;
}

void UpdatingCard::tick(uint32_t now_ms) {
    now_ms_ = now_ms;
}

void UpdatingCard::invalidate() {
    full_clear_     = true;
    last_pct_drawn_ = 255;
}

bool UpdatingCard::isActive() const {
    auto s = um_.status().state;
    return s == UpdateManager::State::Downloading
        || s == UpdateManager::State::InstallReady;
}

bool UpdatingCard::isDirty() const {
    if (!isActive()) return false;
    auto st = um_.status();
    uint8_t pct = (st.bytes_total > 0)
                  ? (uint8_t)((st.bytes_received * 100u) / st.bytes_total)
                  : 0;
    return full_clear_
        || pct != last_pct_drawn_
        || (now_ms_ - last_render_ms_) >= kMinRedrawIntervalMs;
}

void UpdatingCard::render(Display& display) {
    auto& tft  = display.tft();
    auto  st   = um_.status();
    auto* rel  = um_.latestRelease();

    uint8_t pct = (st.bytes_total > 0)
                  ? (uint8_t)((st.bytes_received * 100u) / st.bytes_total)
                  : 0;

    if (full_clear_) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextWrap(false);

        tft.setTextSize(2);
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
        tft.setCursor(8, 6);
        tft.print("UPDATING");

        tft.setTextSize(1);
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
        tft.setCursor(8, 30);
        tft.print("v");
        tft.print(um_.currentVersion());
        tft.print(" -> v");
        tft.print(rel ? rel->tag : "?");

        // Progress bar outline (drawn once per appearance).
        tft.drawRect(8, 60, 224, 16, ST77XX_WHITE);

        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.setCursor(8, 110);
        tft.print("Do not unplug");

        full_clear_ = false;
    }

    // Per-frame: erase + refill just the bar interior and the % label.
    // (CLAUDE.md: never fillScreen per frame.)
    tft.fillRect(9, 61, 222, 14, ST77XX_BLACK);
    uint16_t fill = (uint16_t)((222u * pct) / 100u);
    if (fill > 0) tft.fillRect(9, 61, fill, 14, ST77XX_GREEN);

    tft.fillRect(8, 84, 80, 16, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setCursor(8, 84);
    tft.print(pct);
    tft.print("%");

    last_pct_drawn_ = pct;
    last_render_ms_ = now_ms_;
}
