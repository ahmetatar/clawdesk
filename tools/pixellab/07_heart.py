#!/usr/bin/env python3
# Oksama (ANIM_LOVE) icin PixelLab pixel-art KALP sprite'i uret.
# clawd deterministik kalir; PixelLab yalniz "kalp" elementini uretir (hibrit recete).
# Cikti: out/heart.png (transparan zemin) -> clawd_anim.py yukselen kalpler icin kullanir.
#   source tools/pixellab/secrets.sh && SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem \
#     python3 tools/pixellab/07_heart.py [SIZE]
import sys, os, lib

size = int(sys.argv[1]) if len(sys.argv) > 1 else 32
os.makedirs("out", exist_ok=True)

print(f"bitforge kalp -> {size}x{size}")
res = lib.post("/create-image-bitforge", {
    "description": "a single small cute heart, bright warm red with a soft glossy pink highlight in the top-left, clean simple pixel art, centered on a flat solid black background, no text, no border",
    "image_size": {"width": size, "height": size},
})
lib.save_image_field(res["image"], "out/heart.png")
print("maliyet $", res.get("usage", {}).get("usd"))
