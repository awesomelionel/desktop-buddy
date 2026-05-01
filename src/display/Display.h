#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// 1.14" Reverse TFT Feather: 240x135 native landscape, rotation 1 = 90° clockwise.
class Display {
public:
    static constexpr int W = 240;
    static constexpr int H = 135;

    Display();

    void begin();

    Adafruit_ST7789& tft() { return tft_; }
    int width()  const { return W; }
    int height() const { return H; }

    // Backlight control. setBacklight(false) cuts the TFT_BACKLITE pin to
    // low (display goes dark; pixels keep their state). setBacklight(true)
    // restores it. isAsleep() reflects the last call.
    void setBacklight(bool on);
    bool isAsleep() const { return asleep_; }

private:
    Adafruit_ST7789 tft_;
    bool            asleep_ = false;
};
