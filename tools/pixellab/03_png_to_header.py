#!/usr/bin/env python3
# Turn the PNG frames in out/anim_<name> into a single RGB565 C header.
# Transparent pixels are composited onto the device background color.
#
#   python3 tools/pixellab/03_png_to_header.py out/anim_idle clawd_idle
import sys, os, glob
from PIL import Image

BG = (36, 39, 44)  # device background — must match BG_R/G/B in include/config.h
src = sys.argv[1]
name = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(src).replace("anim_", "")

frames = sorted(glob.glob(os.path.join(src, "frame_*.png")))
if not frames:
    raise SystemExit("no frames found in: " + src)

def to565(r, g, b): return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

imgs = []
for fp in frames:
    im = Image.open(fp).convert("RGBA")
    bg = Image.new("RGBA", im.size, BG + (255,))
    imgs.append(Image.alpha_composite(bg, im).convert("RGB"))
W, H = imgs[0].size
N = len(imgs)

out = f"out/{name}.h"
with open(out, "w") as f:
    f.write(f"// clawd animation '{name}': {N} frames, {W}x{H}, RGB565 (PixelLab)\n#pragma once\n")
    f.write(f"#define {name.upper()}_W {W}\n#define {name.upper()}_H {H}\n#define {name.upper()}_FRAMES {N}\n")
    f.write(f"static const uint16_t {name}[{name.upper()}_FRAMES][{name.upper()}_W*{name.upper()}_H] = {{\n")
    for im in imgs:
        px = im.load()
        vals = [to565(*px[x, y]) for y in range(H) for x in range(W)]
        f.write("{" + ",".join(f"0x{v:04X}" for v in vals) + "},\n")
    f.write("};\n")
print(f"wrote {out}  ({N} frames {W}x{H}, ~{N*W*H*2//1024} KB flash)")
