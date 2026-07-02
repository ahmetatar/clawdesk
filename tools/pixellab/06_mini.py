#!/usr/bin/env python3
# Mini clawd sprite -> tek RGB565 C header (src/anims/clawd_mini.h).
# Buyuk maskotun YANINDA gezinen kucuk clawd'lar bunu kullanir (alt-agent gorsellestirme).
# Kaynak = out/clawd_south.png (sadik rigged clawd, tek kare). Alfa ile kirpilir,
# MINI boyutuna sigdirilir, fume zemine kompoze edilir (cihaz yan bandi ile AYNI zemin
# -> cihazda seffaflik derdi yok; eski kareyi BG ile silip yenisini cizer).
#
#   python3 tools/pixellab/06_mini.py
import os
from PIL import Image

BG = (36, 39, 44)   # include/config.h BG_R/G/B ile AYNI olmali
MINI = 40           # mini clawd kare boyutu (px). Yan sutun 64 genis -> gezinme payi var.
SRC = "out/clawd_south.png"
OUT = "out/clawd_mini.h"
NAME = "clawd_mini"


def to565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


im = Image.open(SRC).convert("RGBA")

# 1) clawd'i alfa sinirlarindan kirp (seffaf kenar bosluklarini at)
bbox = im.getbbox()
if bbox:
    im = im.crop(bbox)

# 2) kareye tamamla (en-boy oranini koru, seffaf pad)
w, h = im.size
side = max(w, h)
sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
sq.paste(im, ((side - w) // 2, (side - h) // 2))

# 3) MINI'ye kucult (LANCZOS: blocky karakter kucukte de temiz kalir)
sq = sq.resize((MINI, MINI), Image.LANCZOS)

# 4) fume zemine kompoze et (kenar anti-alias fume'a karisir = yan bant zemini)
bg = Image.new("RGBA", sq.size, BG + (255,))
rgb = Image.alpha_composite(bg, sq).convert("RGB")

px = rgb.load()
vals = [to565(*px[x, y]) for y in range(MINI) for x in range(MINI)]

with open(OUT, "w") as f:
    f.write(f"// mini clawd (yan gezinen alt-agent maskotu): {MINI}x{MINI}, RGB565.\n")
    f.write("// Uretim: python3 tools/pixellab/06_mini.py  (kaynak out/clawd_south.png)\n")
    f.write("#pragma once\n#include <Arduino.h>\n")
    f.write(f"#define CLAWD_MINI_W {MINI}\n#define CLAWD_MINI_H {MINI}\n")
    f.write(f"static const uint16_t {NAME}[CLAWD_MINI_W*CLAWD_MINI_H] = {{")
    f.write(",".join(f"0x{v:04X}" for v in vals))
    f.write("};\n")

print(f"yazildi {OUT}  ({MINI}x{MINI}, ~{MINI*MINI*2//1024} KB flash)")
