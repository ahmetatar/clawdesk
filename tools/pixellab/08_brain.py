#!/usr/bin/env python3
# Generate the brain sprite used by ANIM_BRAIN_FULL and ANIM_COMPACT. Same hybrid
# recipe as 07_heart.py. Output: out/brain.png on a flat background, which
# clawd_anim.py keys out by corner color and then uses as a fill mask.
#   source tools/pixellab/secrets.sh && SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem \
#     python3 tools/pixellab/08_brain.py [SIZE]
import sys, os, lib

size = int(sys.argv[1]) if len(sys.argv) > 1 else 40
os.makedirs("out", exist_ok=True)

print(f"bitforge brain -> {size}x{size}")
res = lib.post("/create-image-bitforge", {
    "description": "simple flat clipart icon of a pink human brain, side profile silhouette, "
                   "instantly recognizable brain shape like a logo icon, only 4-5 big smooth "
                   "thick swirl fold lines in darker pink/magenta (not busy noisy texture, not "
                   "cauliflower texture), solid bubblegum pink fill, thin clean black outline, "
                   "small soft white highlight top-left, small tan brainstem at the bottom "
                   "center, flat minimal shading, no face, no eyes, no mouth, no body, no "
                   "legs, just the brain icon floating alone, centered on a flat solid gray "
                   "background, no text, no border",
    "image_size": {"width": size, "height": size},
})
lib.save_image_field(res["image"], "out/brain.png")
print("cost $", res.get("usage", {}).get("usd"))
