#!/usr/bin/env python3
"""Render a clean mockup of the Bus Arrivals card for docs / the web UI.

Geometry mirrors src/ui/cards/BusCard.cpp exactly:
  kBodyTopY=18, kRowH=19, kCol_Service=8, kCol_Dot=72 (+5 = dot centre),
  kCol_Eta=110, kCol_Type=210; header at (8,4) size 1; rows size 2 at text_y=row_y+1.
Adafruit GFX classic font: a size-1 glyph occupies a 6x8 cell (6 px advance,
baseline +7); size-2 doubles that (12 px advance, baseline +14).

Output format is chosen by the extension:
  .svg  -> tiny vector, browser-native (used by the web UI)
  .png  -> rasterised via Pillow + SF-Mono-Bold (used for docs)

Usage:
  tools/render_bus_card.py [output]            # default data/bus_card_mockup.svg
  tools/render_bus_card.py docs/preview.png 8  # png at 8x scale
"""
import glob
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, "data", "bus_card_mockup.svg")
SCALE = int(sys.argv[2]) if len(sys.argv) > 2 else 8   # png only

# --- device geometry (BusCard.cpp) ---
W, H = 240, 135
kBodyTopY, kRowH = 18, 19
kCol_Service, kCol_Dot, kCol_Eta, kCol_Type = 8, 72, 110, 210

# --- colours (RGB565 from BusCard.cpp -> RGB888 hex) ---
BG      = "#000000"
WHITE   = "#ffffff"
DIVIDER = "#3a3d3a"   # 0x39E7
GREEN   = "#00ff00"   # ST77XX_GREEN  (SEA / seats available)
YELLOW  = "#ffff00"   # ST77XX_YELLOW (Arr)
CYAN    = "#00ffff"   # ST77XX_CYAN   (DD)
TYPE_SD = "#c5c2c5"   # 0xC618        (SD)

# --- card content (the "Downstairs" stop) ---
HEADER = "Downstairs"
ROWS = [
    ("13",   "2m",  WHITE,  "DD", CYAN),
    ("52",   "Arr", YELLOW, "SD", TYPE_SD),
    ("54",   "3m",  WHITE,  "SD", TYPE_SD),
    ("88",   "2m",  WHITE,  "DD", CYAN),
    ("162",  "16m", WHITE,  "SD", TYPE_SD),
    ("410G", "7m",  WHITE,  "SD", TYPE_SD),
]


def render_svg(path):
    """One <text> per field with an explicit textLength so the column widths
    are exact regardless of which monospace font the browser picks."""
    FONT = "ui-monospace,'SF Mono',Menlo,Consolas,monospace"
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
        f'font-family="{FONT}" font-weight="bold">',
        f'<rect width="{W}" height="{H}" fill="{BG}"/>',
    ]

    def text(s, x, baseline, color, advance, font_px):
        esc = s.replace("&", "&amp;").replace("<", "&lt;")
        out.append(
            f'<text x="{x}" y="{baseline}" font-size="{font_px}" fill="{color}" '
            f'textLength="{len(s) * advance}" lengthAdjust="spacing">{esc}</text>')

    # header (size 1: 6 px advance, baseline at 4+7=11) + divider
    text(HEADER, 8, 11, WHITE, 6, 9)
    out.append(f'<rect x="0" y="14" width="{W}" height="1" fill="{DIVIDER}"/>')

    # rows (size 2: 12 px advance, baseline at text_y+14)
    for i, (svc, eta, eta_c, typ, typ_c) in enumerate(ROWS):
        row_y = kBodyTopY + i * kRowH
        baseline = row_y + 1 + 14
        text(svc, kCol_Service, baseline, WHITE, 12, 17)
        out.append(f'<circle cx="{kCol_Dot + 5}" cy="{row_y + 9}" r="5" '
                   f'fill="{GREEN}"/>')
        text(eta, kCol_Eta,  baseline, eta_c, 12, 17)
        text(typ, kCol_Type, baseline, typ_c, 12, 17)

    out.append("</svg>\n")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print("wrote", path, os.path.getsize(path), "bytes (svg)")


def render_png(path, scale):
    from PIL import Image, ImageDraw, ImageFont
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

    def font_for_advance(target):
        size = 4
        while True:
            f = ImageFont.truetype(font_path, size + 1)
            if f.getlength("8") > target:
                return ImageFont.truetype(font_path, size)
            size += 1

    font_body = font_for_advance(12 * scale)
    font_hdr = font_for_advance(6 * scale)
    img = Image.new("RGB", (W * scale, H * scale), BG)
    d = ImageDraw.Draw(img)

    def text(s, x, baseline, color, font):
        d.text((x * scale, baseline * scale), s, font=font, fill=color, anchor="ls")

    text(HEADER, 8, 11, WHITE, font_hdr)
    d.rectangle([0, 14 * scale, W * scale, 15 * scale - 1], fill=DIVIDER)
    for i, (svc, eta, eta_c, typ, typ_c) in enumerate(ROWS):
        row_y = kBodyTopY + i * kRowH
        baseline = row_y + 1 + 14
        text(svc, kCol_Service, baseline, WHITE, font_body)
        cx, cy, r = (kCol_Dot + 5) * scale, (row_y + 9) * scale, 5 * scale
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=GREEN)
        text(eta, kCol_Eta,  baseline, eta_c, font_body)
        text(typ, kCol_Type, baseline, typ_c, font_body)
    img.save(path)
    print("wrote", path, img.size, os.path.getsize(path), "bytes (png)")


if OUT.lower().endswith(".svg"):
    render_svg(OUT)
elif OUT.lower().endswith(".png"):
    render_png(OUT, SCALE)
else:
    raise SystemExit("output must end in .svg or .png")
