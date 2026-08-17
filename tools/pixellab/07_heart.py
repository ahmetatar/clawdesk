#!/usr/bin/env python3
# Generate the pixel-art heart sprite used by ANIM_LOVE. clawd itself stays
# deterministic; PixelLab only produces the heart element (the hybrid recipe).
# Output: out/heart.png, consumed by clawd_anim.py for the rising hearts.
#   source tools/pixellab/secrets.sh && SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem \
#     python3 tools/pixellab/07_heart.py [SIZE]
import sys, os, lib

size = int(sys.argv[1]) if len(sys.argv) > 1 else 32
os.makedirs("out", exist_ok=True)

print(f"bitforge heart -> {size}x{size}")
res = lib.post("/create-image-bitforge", {
    "description": "a single small cute heart, bright warm red with a soft glossy pink highlight in the top-left, clean simple pixel art, centered on a flat solid black background, no text, no border",
    "image_size": {"width": size, "height": size},
})
lib.save_image_field(res["image"], "out/heart.png")
print("cost $", res.get("usage", {}).get("usd"))
