#!/usr/bin/env python3
"""Render a clean mockup of the Bus Arrivals card for docs / the web UI.

Geometry mirrors src/ui/cards/BusCard.cpp exactly:
  kBodyTopY=18, kRowH=19, kCol_Service=8, kCol_Dot=72 (+5 = dot centre),
  kCol_Eta=110, kCol_Type=210; header at (8,4) size 1; rows size 2 at text_y=row_y+1.
Adafruit GFX classic font: a size-1 glyph occupies a 6x8 cell (6 px advance,
baseline +7); size-2 doubles that (12 px advance, baseline +14). SF-Mono-Bold
is sized so its monospace advance equals the device advance, then drawn on
the device baseline.

Usage:
  tools/render_bus_card.py [scale] [output.png]
Defaults: scale 3, output data/bus_card_mockup.png (relative to repo root).
"""
import glob
import os
import sys
from PIL import Image, ImageDraw, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = int(sys.argv[1]) if len(sys.argv) > 1 else 3
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "data", "bus_card_mockup.png")
W, H = 240 * S, 135 * S

# --- colours (RGB565 values from BusCard.cpp, expanded to RGB888) ---
BG      = (0, 0, 0)
WHITE   = (255, 255, 255)
DIVIDER = (58, 61, 58)      # 0x39E7
GREEN   = (0, 255, 0)       # ST77XX_GREEN  (SEA / seats available)
YELLOW  = (255, 255, 0)     # ST77XX_YELLOW (Arr)
CYAN    = (0, 255, 255)     # ST77XX_CYAN   (DD)
TYPE_SD = (197, 194, 197)   # 0xC618        (SD)

# --- font ---
font_path = None
for pat in ("/System/Library/Fonts/SF-Mono-Bold.otf",
            "/System/Library/Fonts/Supplemental/SF-Mono-Bold.otf",
            "/Library/Fonts/SF-Mono-Bold.otf",
            "~/Library/Fonts/SF-Mono-Bold.otf"):
    hits = glob.glob(os.path.expanduser(pat))
    if hits:
        font_path = hits[0]
        break
if font_path is None:
    raise SystemExit("SF-Mono-Bold.otf not found")


def font_for_advance(target_advance):
    """Largest font size whose monospace advance is <= target_advance px."""
    size = 4
    while True:
        f = ImageFont.truetype(font_path, size + 1)
        if f.getlength("8") > target_advance:
            return ImageFont.truetype(font_path, size)
        size += 1


font_body = font_for_advance(12 * S)   # size-2 device text
font_hdr  = font_for_advance(6 * S)    # size-1 device text

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)


def draw_text(s, x, baseline, color, font):
    """Draw `s` with its left edge at device-px `x` and the font baseline at
    device-px `baseline` (anchor 'ls' = left / baseline)."""
    d.text((x * S, baseline * S), s, font=font, fill=color, anchor="ls")


# --- header: setCursor(8, 4) size 1 -> glyph y 4..10, baseline at 11 ---
draw_text("Downstairs", 8, 11, WHITE, font_hdr)
d.rectangle([0, 14 * S, W, 15 * S - 1], fill=DIVIDER)

# --- rows: (service, eta, eta_colour, type, type_colour) ---
rows = [
    ("13",   "2m",  WHITE,  "DD", CYAN),
    ("52",   "Arr", YELLOW, "SD", TYPE_SD),
    ("54",   "3m",  WHITE,  "SD", TYPE_SD),
    ("88",   "2m",  WHITE,  "DD", CYAN),
    ("162",  "16m", WHITE,  "SD", TYPE_SD),
    ("410G", "7m",  WHITE,  "SD", TYPE_SD),
]

kBodyTopY, kRowH = 18, 19
kCol_Service, kCol_Dot, kCol_Eta, kCol_Type = 8, 72, 110, 210

for i, (svc, eta, eta_c, typ, typ_c) in enumerate(rows):
    row_y    = kBodyTopY + i * kRowH
    text_y   = row_y + 1            # size-2 glyph top
    baseline = text_y + 14         # size-2 baseline
    draw_text(svc, kCol_Service, baseline, WHITE, font_body)
    # load dot: fillCircle(kCol_Dot+5, row_y+9, 5)
    cx, cy, r = (kCol_Dot + 5) * S, (row_y + 9) * S, 5 * S
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GREEN)
    draw_text(eta, kCol_Eta,  baseline, eta_c, font_body)
    draw_text(typ, kCol_Type, baseline, typ_c, font_body)

img.save(OUT)
print("wrote", OUT, img.size, "| body font px:", font_body.size,
      "| header font px:", font_hdr.size, "|", os.path.getsize(OUT), "bytes")
