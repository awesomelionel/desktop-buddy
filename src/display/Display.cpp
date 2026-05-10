#include "Display.h"

#include <Arduino.h>
#include <algorithm>

Display::Display() : tft_(TFT_CS, TFT_DC, TFT_RST) {}

void Display::begin() {
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);

    // Configure LEDC for the backlight before driving the pin.
    // Arduino-ESP32 v3.x: ledcAttach() replaces ledcSetup + ledcAttachPin.
    ledcAttach(TFT_BACKLITE, kBacklightFreqHz, kBacklightBits);
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
    // Perceptual gamma. Human brightness perception is roughly the cube
    // root of photon flux, so a linear pct->duty mapping makes "40%" look
    // like ~73% brightness. Apply a 2.2 gamma so dim_level_pct=40 actually
    // feels roughly half-bright. Only runs on transitions, so powf is fine.
    float    linear = static_cast<float>(pct) / 100.0f;
    float    gamma  = powf(linear, 2.2f);
    uint32_t duty   = static_cast<uint32_t>(gamma * 255.0f + 0.5f);
    ledcWrite(TFT_BACKLITE, duty);
    current_pct_ = pct;
    asleep_      = (pct == 0);
}
