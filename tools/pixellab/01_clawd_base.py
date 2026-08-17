#!/usr/bin/env python3
# Generate clawd's base pixel sprite. Two methods:
#   A) image-to-pixelart : converts the real clawd artwork to pixel art (most faithful)
#   B) bitforge          : redraws it using the reference as a style hint
# Output: out/clawd_base.png — the reference_image for every animation.
#
#   source tools/pixellab/secrets.sh && python3 tools/pixellab/01_clawd_base.py [A|B] [SIZE]
import sys, os, lib

REF = "/Users/aatar/.claude/image-cache/61a0d505-0034-4fb5-a9d7-7841613935bf/1.png"
mode = (sys.argv[1] if len(sys.argv) > 1 else "A").upper()
size = int(sys.argv[2]) if len(sys.argv) > 2 else 64
os.makedirs("out", exist_ok=True)

if mode == "A":
    print(f"image-to-pixelart -> {size}x{size}")
    res = lib.post("/image-to-pixelart", {
        "image": lib.b64img(REF),
        "image_size":  {"width": 200, "height": 200},
        "output_size": {"width": size, "height": size},
    })
else:
    print(f"bitforge (style reference) -> {size}x{size}")
    res = lib.post("/create-image-bitforge", {
        "description": "clawd: a cute coral/salmon blocky mascot creature, two small square dark eyes, two tiny ears on top, four short stubby legs, minimal, no mouth",
        "image_size": {"width": size, "height": size},
        "style_image": lib.b64img(REF),
        "transparent_background": True,
    })

lib.save_image_field(res["image"], "out/clawd_base.png")
print("cost $", res.get("usage", {}).get("usd"))
