#!/usr/bin/env python3
# Fix the clawd_hacking keyboard by hand, without calling PixelLab again.
#
# Two problems:
#   1) The space bar sat in the BOTTOM row, farthest from the paws — clawd sits
#      behind the keyboard and its paws reach the TOP edge. So the key field is
#      ROTATED by one band: space moves to the top, the other rows shift down.
#      Rotating (rather than cutting) keeps the tall orange key on the right intact.
#   2) Space was never pressed in any frame. On selected frames the paws now
#      stretch 1px down into the bezel row while the space keycap drops 1px and
#      darkens — the same "pressed key" language the other keys already use.
#
# Idempotent: reads out/anim_hacking, writes out/anim_hacking_kb.
#
#   python3 tools/pixellab/10_hacking_space_fix.py
#   python3 tools/pixellab/03_png_to_header.py out/anim_hacking_kb clawd_hacking
#   cp tools/pixellab/out/clawd_hacking.h src/anims/
import os, glob
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "out", "anim_hacking")
DST = os.path.join(HERE, "out", "anim_hacking_kb")

# --- keyboard geometry (measured on the 64x64 sprite) ---
KB_TOP = 29          # first key row
BANDS = 4            # 4 key rows
BAND_H = 4           # 3 keycap rows + 1 gap row
FIELD_H = BANDS * BAND_H          # y29..y44
SPACE_X0, SPACE_X1 = 14, 44       # space bar columns (inclusive)
BEZEL_Y = 28                      # top edge of the keyboard, where the paws reach
PAW_COLS = list(range(24, 28)) + list(range(36, 40))   # the inner pair of paws
PRESS_FRAMES = {2, 6}             # 2 keystrokes per 8 frames

BODY = (60, 58, 78, 255)          # keyboard body, visible above a pressed keycap
PRESSED = (96, 92, 120, 255)      # pressed keycap color, same as the other keys


def rotate_key_field(px):
    """Rotate the key field down by one band, bringing space to the top."""
    src = {y: [px[x, y] for x in range(64)] for y in range(KB_TOP, KB_TOP + FIELD_H)}
    for y in range(KB_TOP, KB_TOP + FIELD_H):
        row = src[KB_TOP + ((y - KB_TOP - BAND_H) % FIELD_H)]
        for x in range(64):
            px[x, y] = row[x]


def press_space(px):
    """Draw space pressed and stretch the inner paws 1px down."""
    for x in range(SPACE_X0, SPACE_X1 + 1):
        px[x, KB_TOP] = BODY              # keycap dropped 1px
        px[x, KB_TOP + 1] = PRESSED
        px[x, KB_TOP + 2] = PRESSED
    for x in PAW_COLS:
        px[x, BEZEL_Y] = px[x, BEZEL_Y - 1]   # carry the paw color down


os.makedirs(DST, exist_ok=True)
frames = sorted(glob.glob(os.path.join(SRC, "frame_*.png")))
if not frames:
    raise SystemExit("no frames found in: " + SRC)

for i, fp in enumerate(frames):
    im = Image.open(fp).convert("RGBA")
    px = im.load()
    rotate_key_field(px)
    if i in PRESS_FRAMES:
        press_space(px)
    out = os.path.join(DST, os.path.basename(fp))
    im.save(out)
    print(("wrote " + out) + ("  [space pressed]" if i in PRESS_FRAMES else ""))
