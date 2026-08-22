#!/usr/bin/env python3
# Generate the pixel-art over-ear headphones sprite used by ANIM_IDLE_MUSIC.
# Same hybrid recipe as 07_heart.py / 08_brain.py, but bitforge (text-only)
# could not hold a symmetric headphones silhouette after 3 tries (drifted into
# perspective renders / bee-eyes / muddy colors). So this draws a clean, exactly
# symmetric reference with PIL first, then sends it through /image-to-pixelart
# for stylization only (shape stays under our control, PixelLab adds the pixel
# art texture/shading polish) — the same trick 01_clawd_base.py mode A uses.
# Output: out/headphones.png, consumed by clawd_anim.py (draw_headphones_spr).
#   source tools/pixellab/secrets.sh && SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem \
#     python3 tools/pixellab/09_headphones.py [OUT_W] [OUT_H]
import sys, os, math
from PIL import Image, ImageDraw
import lib

out_w = int(sys.argv[1]) if len(sys.argv) > 1 else 76
out_h = int(sys.argv[2]) if len(sys.argv) > 2 else 24
os.makedirs("out", exist_ok=True)

# ---------- 1) draw a big, exactly symmetric reference in code ----------
S = 8                      # supersample factor for a clean scaled-down look
W, H = out_w * S, out_h * S
BG    = (150, 148, 160, 255)
NAVY  = (40, 42, 62, 255)          # band + cup rim
SLATE = (96, 100, 128, 255)        # cup fill
SLATE_HI = (150, 154, 182, 255)    # cup highlight
ACC   = (232, 120, 70, 255)        # driver accent (clawd orange)
BLACK = (18, 16, 24, 255)

ref = Image.new("RGBA", (W, H), BG)
d = ImageDraw.Draw(ref)

cup_r = int(H * 0.24)                      # small cups relative to the gap, so
cup_y = int(H * 0.60)                      # they clear clawd's eyes once composited
cup_lx = int(W * 0.11)                     # pushed toward the edges: leaves a
cup_rx = W - cup_lx                        # visible gap in the middle for the face
band_w = int(H * 0.09)

# band: a thick arc from the top of the left cup to the top of the right cup
top = int(H * 0.10)
bbox = [cup_lx - cup_r, top, cup_rx + cup_r, cup_y + cup_r]
d.arc(bbox, 200, 340, fill=BLACK, width=band_w + int(S * 2))
d.arc(bbox, 200, 340, fill=NAVY, width=band_w)

def cup(cx):
    d.ellipse([cx - cup_r - S, cup_y - cup_r - S, cx + cup_r + S, cup_y + cup_r + S], fill=BLACK)
    d.ellipse([cx - cup_r, cup_y - cup_r, cx + cup_r, cup_y + cup_r], fill=NAVY)
    inner = int(cup_r * 0.62)
    d.ellipse([cx - inner, cup_y - inner, cx + inner, cup_y + inner], fill=SLATE)
    # soft top-left highlight (a few offset arcs, cheap fake gradient)
    hi_r = int(inner * 0.75)
    d.ellipse([cx - inner + hi_r * 0.15, cup_y - inner + hi_r * 0.15,
               cx - inner + hi_r * 1.15, cup_y - inner + hi_r * 1.15], fill=SLATE_HI)
    acc_r = int(inner * 0.30)
    d.ellipse([cx - acc_r, cup_y - acc_r, cx + acc_r, cup_y + acc_r], fill=ACC)

cup(cup_lx)
cup(cup_rx)

ref_path = "out/headphones_ref.png"
ref.convert("RGB").save(ref_path)
print("reference:", ref_path, ref.size)

# ---------- 2) PixelLab stylizes it into pixel art at the final size ----------
print(f"image-to-pixelart -> {out_w}x{out_h}")
res = lib.post("/image-to-pixelart", {
    "image": lib.b64img(ref_path),
    "image_size":  {"width": W, "height": H},
    "output_size": {"width": out_w, "height": out_h},
})
lib.save_image_field(res["image"], "out/headphones.png")
print("cost $", res.get("usage", {}).get("usd"))
