#!/usr/bin/env python3
# clawd_hacking klavyesini elle duzeltir (PixelLab'i tekrar cagirmadan).
#
# Iki sorun vardi:
#   1) space bar en ALT sirada duruyordu; maskot klavyenin ARKASINDA oturdugu ve
#      patileri klavyenin UST kenarina indigi icin space patilerden en uzak siraydi.
#      -> tus alanini bir bant DONDURUYORUZ: space en uste (patilerin altina) gelir,
#         diger uc sira birer bant asagi kayar. Sag taraftaki UZUN turuncu tus iki
#         bant boyu oldugu icin dondurme (kesme degil) sart: butun halinde kayar.
#   2) space hicbir frame'de basilmiyordu. -> secili frame'lerde pati 1px asagi
#      uzar (y28 bezel satirina taser) ve ayni anda space keycap'i 1px alcalip
#      koyulasir; bu, diger tuslarda zaten kullanilan "basili tus" dilinin aynisi.
#
# Idempotent: kaynak out/anim_hacking okunur, sonuc out/anim_hacking_kb'ye yazilir.
#
#   python3 tools/pixellab/10_hacking_space_fix.py
#   python3 tools/pixellab/03_png_to_header.py out/anim_hacking_kb clawd_hacking
#   cp tools/pixellab/out/clawd_hacking.h src/anims/
import os, glob
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "out", "anim_hacking")
DST = os.path.join(HERE, "out", "anim_hacking_kb")

# --- klavye geometrisi (64x64 sprite, olculdu) ---
KB_TOP = 29          # ilk tus satiri
BANDS = 4            # 4 tus sirasi
BAND_H = 4           # 3 satir keycap + 1 satir bosluk
FIELD_H = BANDS * BAND_H          # y29..y44
SPACE_X0, SPACE_X1 = 14, 44       # space bar sutunlari (dahil)
BEZEL_Y = 28                      # klavyenin ust kenari (pati buraya tasar)
PAW_COLS = list(range(24, 28)) + list(range(36, 40))   # ic pati cifti = "bas parmak"
PRESS_FRAMES = {2, 6}             # 8 frame'de 2 vurus (~111ms*8 -> ~0.9sn'de bir)

BODY = (60, 58, 78, 255)          # klavye govdesi (basili keycap'in ustunde gorunur)
PRESSED = (96, 92, 120, 255)      # basili keycap rengi (mevcut tuslarla ayni)


def rotate_key_field(px):
    """Tus alanini bir bant asagi dondur: space en uste gelir."""
    src = {y: [px[x, y] for x in range(64)] for y in range(KB_TOP, KB_TOP + FIELD_H)}
    for y in range(KB_TOP, KB_TOP + FIELD_H):
        row = src[KB_TOP + ((y - KB_TOP - BAND_H) % FIELD_H)]
        for x in range(64):
            px[x, y] = row[x]


def press_space(px):
    """Space'i basili ciz + ic patileri 1px asagi uzat."""
    for x in range(SPACE_X0, SPACE_X1 + 1):
        px[x, KB_TOP] = BODY              # keycap 1px alcaldi
        px[x, KB_TOP + 1] = PRESSED
        px[x, KB_TOP + 2] = PRESSED
    for x in PAW_COLS:
        px[x, BEZEL_Y] = px[x, BEZEL_Y - 1]   # pati rengini asagi tasi


os.makedirs(DST, exist_ok=True)
frames = sorted(glob.glob(os.path.join(SRC, "frame_*.png")))
if not frames:
    raise SystemExit("frame bulunamadi: " + SRC)

for i, fp in enumerate(frames):
    im = Image.open(fp).convert("RGBA")
    px = im.load()
    rotate_key_field(px)
    if i in PRESS_FRAMES:
        press_space(px)
    out = os.path.join(DST, os.path.basename(fp))
    im.save(out)
    print(("yazildi " + out) + ("  [space basili]" if i in PRESS_FRAMES else ""))
