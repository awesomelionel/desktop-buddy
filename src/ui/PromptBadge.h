// src/ui/PromptBadge.h
#pragma once

#include <stdint.h>

class Adafruit_ST7789;

namespace ui {

// Permission-prompt badge shown when a prompt is COLLAPSED (waiting
// for the user to acknowledge it). Geometry is shared by every card
// that wants to surface the prompt — currently EyesCard and
// StatusCard — so a center-press always looks the same regardless
// of which carousel page is on screen.
//
//   y = 95 .. 112  (h = 18)         ← badge
//   y = 117 .. 134 (h = 18)         ← footer (separate)
//
// The caller is responsible for erasing the area before drawing —
// see kBadgeEraseY / kBadgeEraseH for the recommended wipe rect (1
// px of breathing room above + below the badge so the border can't
// ghost on the previous frame).
constexpr int kPromptBadgeH         = 18;
constexpr int kPromptBadgeMargin    = 8;
constexpr int kPromptBadgeX         = kPromptBadgeMargin;
constexpr int kPromptBadgeW         = 240 - 2 * kPromptBadgeMargin;
constexpr int kPromptBadgeBottomGap = 4;
constexpr int kPromptBadgeY         = 135 - 18 - kPromptBadgeBottomGap - kPromptBadgeH;  // 95

// Recommended erase rect (1 px breathing room top + bottom).
constexpr int kPromptBadgeEraseY    = kPromptBadgeY - 1;
constexpr int kPromptBadgeEraseH    = kPromptBadgeH + 2;

// Orange used for the leading "?" glyph; matches kQColor in EyesCard
// (RGB565 r=31, g=29, b=0).
constexpr uint16_t kPromptBadgeQColor = 0xFBA0;

void drawPromptBadge(Adafruit_ST7789& tft, const char* tool);

}  // namespace ui
