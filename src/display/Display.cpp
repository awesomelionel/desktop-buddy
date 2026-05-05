#include "Display.h"

#include <Arduino.h>
#include <algorithm>

Display::Display() : tft_(TFT_CS, TFT_DC, TFT_RST) {}

void Display::begin() {
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);

    // Configure LEDC for the backlight before driving the pin.
    ledcSetup(kBacklightChannel, kBacklightFreqHz, kBacklightBits);
    ledcAttachPin(TFT_BACKLITE, kBacklightChannel);
    current_pct_ = 0;
    setBacklight(static_cast<uint8_t>(100));

    tft_.init(135, 240);
    tft_.setRotation(1);
    tft_.fillScreen(ST77XX_BLACK);
    tft_.setTextWrap(false);
}

void Display::setBacklight(uint8_t pct) {
    if (pct > 100) pct = 100;
    if (pct == current_pct_) return;
    uint32_t duty = (static_cast<uint32_t>(pct) * 255UL) / 100UL;
    ledcWrite(kBacklightChannel, duty);
    current_pct_ = pct;
    asleep_      = (pct == 0);
}
