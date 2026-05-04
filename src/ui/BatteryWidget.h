// src/ui/BatteryWidget.h
#pragma once

#include <stdint.h>

class Adafruit_ST7789;

namespace ui {

// Right-anchored battery widget rendered into the StatusCard footer
// band. Layout (all in pixels, screen 240×135):
//
//   x:  180 .................... 234
//       [   "%%%" ][ █ █ █ █ ►]      ← y = 120 .. 130 (11 px tall)
//                  body  tip
//
// The percent text is drawn first (right-aligned at x=205) and the
// icon body sits to its right. Four discrete bars inside the body
// represent state-of-charge thresholds:
//
//   pct >= 75 : 4 bars   (green)
//   pct >= 50 : 3 bars   (green)
//   pct >= 25 : 2 bars   (yellow)
//   pct >= 10 : 1 bar    (red)
//   pct <  10 : empty    (red outline + red text)
//
// While charging the caller passes anim_step ∈ [0..3] which renders
// 1, 2, 3, or 4 bars (in green) regardless of percent — that gives
// the increasing-bar animation the user asked for. The caller is
// responsible for stepping anim_step on a timer.
//
// The helper paints the widget directly to the TFT and assumes the
// caller has already erased the area to black; see kEraseRect for
// the exact bounding box to wipe before redrawing.
constexpr int kBatteryEraseX = 180;
constexpr int kBatteryEraseY = 120;
constexpr int kBatteryEraseW = 55;
constexpr int kBatteryEraseH = 11;

void drawBatteryWidget(Adafruit_ST7789& tft,
                       uint8_t percent,
                       bool    charging,
                       uint8_t anim_step);

}  // namespace ui
