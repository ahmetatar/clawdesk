#!/usr/bin/env python3
# Usage ekranindaki saat icin ozel GFX fontu uretir (src/fonts/clock_font.h).
#
# NEDEN: TFT_eSPI'nin hazir kalin fontlari 18pt (rakam ~25px) ve 24pt (~35px);
# saat icin "24pt'ten cok az kucuk" bir boy istendi -> arada boy yok. Bu script
# Helvetica Bold'dan (FreeSansBold'a metrik olarak cok yakin) YALNIZ saatin
# ihtiyaci olan glifleri ('-' '.' '/' '0'-'9' ':') iceren ~31px'lik bir
# Adafruit-GFX fontu uretir (kucuk glif seti -> header kucuk kalir).
#
# Kullanim: python3 tools/make_clock_font.py   (cikti: src/fonts/clock_font.h)
# Boy degistirmek icin TARGET_DIGIT_H'yi ayarla, yeniden calistir, derle.

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/System/Library/Fonts/Helvetica.ttc"
FONT_INDEX = 1                     # Helvetica Bold
TARGET_DIGIT_H = 31                # '0' glif yuksekligi (px). FSB24=35, FSB18=25.
FIRST, LAST = 0x2D, 0x3A           # '-' '.' '/' '0'..'9' ':'
NAME = "ClockBold"

def digit_height(size):
    f = ImageFont.truetype(FONT_PATH, size, index=FONT_INDEX)
    l, t, r, b = f.getbbox("0")
    return b - t

# em boyunu hedef rakam yuksekligine gore sec
size = TARGET_DIGIT_H
while digit_height(size) < TARGET_DIGIT_H:
    size += 1
while digit_height(size) > TARGET_DIGIT_H and size > 4:
    size -= 1
font = ImageFont.truetype(FONT_PATH, size, index=FONT_INDEX)
ascent, descent = font.getmetrics()

bitmap = []                        # bit-akisi (MSB-first paketlenir)
glyphs = []                        # (offset, w, h, xAdv, xOff, yOff)
for code in range(FIRST, LAST + 1):
    ch = chr(code)
    xadv = round(font.getlength(ch))
    l, t, r, b = font.getbbox(ch)
    w, h = r - l, b - t
    off = len(bitmap) // 8
    if w > 0 and h > 0:
        img = Image.new("L", (w + 8, ascent + descent + 8), 0)
        ImageDraw.Draw(img).text((-l + 4, 4), ch, fill=255, font=font)
        px = img.load()
        for y in range(t, b):
            for x in range(4, 4 + w):
                bitmap.append(1 if px[x, y + 4] >= 128 else 0)
    else:
        w = h = 0
    # yOff: baseline'dan glif ustune (negatif = yukari). PIL'de baseline=ascent.
    glyphs.append((off, w, h, xadv, max(l, 0), t - ascent))

while len(bitmap) % 8:
    bitmap.append(0)
data = bytes(int("".join(map(str, bitmap[i:i+8])), 2) for i in range(0, len(bitmap), 8))

out = [
    "#pragma once",
    f"// Otomatik uretildi: tools/make_clock_font.py (Helvetica Bold ~{size}px em,",
    f"// rakam yuksekligi {digit_height(size)}px). Glifler: '-' '.' '/' '0'-'9' ':'.",
    "// Elle DUZENLEME; boy icin script'teki TARGET_DIGIT_H'yi degistirip yeniden uret.",
    "",
    f"const uint8_t {NAME}Bitmaps[] PROGMEM = {{",
]
for i in range(0, len(data), 12):
    out.append("  " + ", ".join(f"0x{b:02X}" for b in data[i:i+12]) + ",")
out.append("};")
out.append("")
out.append(f"const GFXglyph {NAME}Glyphs[] PROGMEM = {{")
for (off, w, h, xa, xo, yo), code in zip(glyphs, range(FIRST, LAST + 1)):
    out.append(f"  {{ {off:5d}, {w:3d}, {h:3d}, {xa:3d}, {xo:4d}, {yo:4d} }},   // 0x{code:02X} '{chr(code)}'")
out.append("};")
out.append("")
out.append(f"const GFXfont {NAME} PROGMEM = {{")
out.append(f"  (uint8_t  *){NAME}Bitmaps,")
out.append(f"  (GFXglyph *){NAME}Glyphs,")
out.append(f"  0x{FIRST:02X}, 0x{LAST:02X}, {ascent + descent} }};")
out.append("")

import os
dst = os.path.join(os.path.dirname(__file__), "..", "src", "fonts", "clock_font.h")
os.makedirs(os.path.dirname(dst), exist_ok=True)
with open(dst, "w") as f:
    f.write("\n".join(out))
print(f"OK: {os.path.normpath(dst)}  em={size}px  digitH={digit_height(size)}  glifler={len(glyphs)}  bitmap={len(data)}B")
