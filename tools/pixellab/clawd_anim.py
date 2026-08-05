#!/usr/bin/env python3
# clawd DETERMINISTIK animasyon modulu — PixelLab'in temiz karakter frame'i uzerinde
# kod ile hareket. Amac: HER sahnede clawd AYNI boyut/konum (sabit 64x64 tuval),
# hareket temiz (piksel yeniden cizilmez -> bozulma yok), ifade farki ayirt-edici ogeyle.
#
#   python3 clawd_anim.py <ad>      # idle | hacking  (out/anim_<ad>/ + out/<ad>_montage.png)
#   python3 clawd_anim.py all
import sys, os, glob, shutil, math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BG = (36, 39, 44)  # fume (montaj onizleme; include/config.h + 03_png_to_header.py ile AYNI)

# --- kanonik clawd: sadik (rotate_character) karakterin temiz south rotation'i ---
# Sabit yol; yoksa char_zip2 export'undan cek.
_srcpath = os.path.join(HERE, "out/clawd_south.png")
if not os.path.exists(_srcpath):
    _srcpath = glob.glob(os.path.join(HERE, "out/char_zip2/*/rotations/south.png"))[0]
_src = Image.open(_srcpath).convert("RGBA")
CLAWD = _src.crop(_src.getchannel("A").getbbox())
CW, CH = CLAWD.size                       # 44 x 30
_px = CLAWD.load()

# SABIT yerlesim — tum animasyonlarda ayni (boyut + konum tutarli)
CANVAS = 64
CX = (CANVAS - CW) // 2                    # yatay orta
CY = (CANVAS - CH) // 2                     # DIKEY ORTA (tum sahnelerde clawd ekran ortasinda)

# clawd yapisi (piksel analizinden)
FACE = (214, 82, 56, 255)
_dark = [(x, y) for y in range(CH) for x in range(CW) if _px[x, y][3] > 0 and sum(_px[x, y][:3]) < 170]
EYE_COL = _px[_dark[0][0], _dark[0][1]]
EYES = _dark                                       # goz pikselleri
ARM_Y = range(7, 15)                               # kollarin y bandi
LARM = [(x, y) for y in ARM_Y for x in range(0, 7)  if _px[x, y][3] > 0]   # sol kol (govde disi)
RARM = [(x, y) for y in ARM_Y for x in range(37, CW) if _px[x, y][3] > 0]  # sag kol
# ayaklar (y23-29): sol cift x7-17, sag cift x26-36
LEGS_L = [(x, y) for y in range(23, CH) for x in range(7, 18)  if _px[x, y][3] > 0]
LEGS_R = [(x, y) for y in range(23, CH) for x in range(26, 37) if _px[x, y][3] > 0]

def _shift(im, pixels, dx, dy):
    """pixels bolgesini (dx,dy) kaydir: eskiyi temizle, orijinal rengiyle yeniden ciz."""
    if not (dx or dy): return
    p = im.load()
    saved = [(x, y, im.getpixel((x, y))) for (x, y) in pixels]
    for (x, y) in pixels: p[x, y] = (0, 0, 0, 0)
    for (x, y, col) in saved:
        nx, ny = x + dx, y + dy
        if 0 <= nx < CW and 0 <= ny < CH: p[nx, ny] = col

def _happy_eyes(im):
    """Gozleri sil, resimdeki gibi INCE (1px) temiz > (sol) ve < (sag) ciz."""
    p = im.load()
    for (x, y) in EYES: p[x, y] = FACE
    gt = [(11,3),(12,4),(12,5),(11,6)]     # ince ">" vertex sagda
    lt = [(32,3),(31,4),(31,5),(32,6)]     # ince "<" vertex solda
    for (x, y) in gt + lt:
        if 0 <= x < CW and 0 <= y < CH: p[x, y] = EYE_COL

def _sleep_eyes(im):
    """Gozleri KAPALI cizgi yap: karesel gozleri sil, her goz icin ince yatay cizgi."""
    p = im.load()
    for (x, y) in EYES: p[x, y] = FACE
    left  = [(x, y) for (x, y) in EYES if x < CW // 2]
    right = [(x, y) for (x, y) in EYES if x >= CW // 2]
    for cl in (left, right):
        if not cl: continue
        xs = [x for x, y in cl]; ys = [y for x, y in cl]
        cy = (min(ys) + max(ys)) // 2
        for x in range(min(xs) - 1, max(xs) + 2):        # hafif tasan ince cizgi
            if 0 <= x < CW: p[x, cy] = EYE_COL

def clawd_variant(eye_dx=0, eye_dy=0, larm_dy=0, rarm_dy=0, eyes="normal",
                  lleg_dy=0, rleg_dy=0):
    """clawd kopyasi. eyes='happy' -> > < ; eyes='sleep' -> kapali cizgi;
    gozler (eye_dx,eye_dy) kayar; kollar VE ayaklar dikey kaydirilir."""
    im = CLAWD.copy(); p = im.load()
    # --- gozler ---
    if eyes == "happy":
        _happy_eyes(im)
    elif eyes == "sleep":
        _sleep_eyes(im)
    elif eye_dx or eye_dy:
        for (x, y) in EYES: p[x, y] = FACE
        for (x, y) in EYES:
            nx = min(CW - 1, max(0, x + eye_dx)); ny = min(CH - 1, max(0, y + eye_dy))
            p[nx, ny] = EYE_COL
    # --- kollar VE ayaklar: hepsi dikey _shift (kol yontemi -> temiz, bozulmasiz) ---
    _shift(im, LARM, 0, larm_dy)
    _shift(im, RARM, 0, rarm_dy)
    _shift(im, LEGS_L, 0, lleg_dy)
    _shift(im, LEGS_R, 0, rleg_dy)
    return im

# --- kalp (kucuk), cesitli ekran noktalarinda cikip kaybolur ---
HEART = (222, 40, 44, 255); HEART_HI = (255, 90, 80, 255)   # kirmizi
_HEART_PX = [(1,0),(3,0),(0,1),(1,1),(2,1),(3,1),(4,1),
             (0,2),(1,2),(2,2),(3,2),(4,2),(1,3),(2,3),(3,3),(2,4)]
def draw_heart(c, cx, cy):
    p = c.load()
    for (x, y) in _HEART_PX:
        px_, py_ = cx + x, cy + y
        if 0 <= px_ < CANVAS and 0 <= py_ < CANVAS:
            p[px_, py_] = HEART
    if 0 <= cx+1 < CANVAS and 0 <= cy+1 < CANVAS: p[cx+1, cy+1] = HEART_HI  # parlak nokta

# --- PixelLab pixel-art KALP (07_heart.py ile uretildi) — oksama/love icin ---
# clawd deterministik kalir; PixelLab yalniz bu "kalp" elementini uretir (hibrit
# recete: tum karakteri PixelLab animasyonuna sokmak clawd'i bozuyordu). Zemin duz
# gri geldi -> kose renginden key-out ile transparanlastir, sonra bbox'a kirp.
def _load_heart_spr():
    hp = os.path.join(HERE, "out/heart.png")
    if not os.path.exists(hp): return None
    im = Image.open(hp).convert("RGBA"); px = im.load(); w, h = im.size
    bg = px[0, 0][:3]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if all(abs((r, g, b)[k] - bg[k]) <= 45 for k in range(3)):
                px[x, y] = (0, 0, 0, 0)                      # gri zemini sil
    bb = im.getchannel("A").getbbox()
    return im.crop(bb) if bb else None
HEART_SPR = _load_heart_spr()

def draw_heart_spr(c, cx, cy, th=13, alpha=255):
    """PixelLab kalbini (cx,cy) MERKEZLI, th px yuksekliginde, alpha ile birlestir.
    Sprite yoksa kod-cizili minik kalbe duser (guvenli fallback)."""
    if HEART_SPR is None:
        return draw_heart(c, cx, cy)
    w, h = HEART_SPR.size
    tw = max(1, round(w * th / h))
    spr = HEART_SPR.resize((tw, th), Image.NEAREST)
    if alpha < 255:
        spr.putalpha(spr.getchannel("A").point(lambda v: v * alpha // 255))
    c.alpha_composite(spr, (cx - tw // 2, cy - th // 2))

def base_canvas():
    return Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))

def place(canvas, clawd_img, dx=0, dy=0):
    canvas.alpha_composite(clawd_img, (CX + dx, CY + dy))

# ---------- modern klavye (yukaridan bakis, renkli Enter + aksan tuslar) ----------
KB_CASE = (40, 38, 56, 255)      # kasa (koyu cerceve)
KB_DECK = (60, 58, 78, 255)      # ic yuzey
KB_LIP  = (30, 28, 44, 255)      # on kalinlik (alt kenar)
CAP     = (168, 164, 192, 255)   # tus kapagi (acik gri)
CAP_SH  = (112, 108, 138, 255)   # tus alt golge (3B his)
CAP_DN  = (96, 92, 120, 255)     # basili tus
ENTER   = (232, 120, 70, 255)    # renkli Enter (clawd turuncusu)
ENTER_SH= (170, 78, 44, 255)
ESC     = (210, 90, 96, 255)     # aksan (Esc, kirmizimsi)
ACCENT  = (96, 180, 170, 255)    # aksan (birkac tus, teal)

def _cap(d, x0, y0, x1, y1, col, sh, pressed):
    if pressed:
        d.rectangle([x0, y0 + 1, x1, y1], fill=CAP_DN)
    else:
        d.rectangle([x0, y0, x1, y1 - 1], fill=col)
        d.line([(x0, y1), (x1, y1)], fill=sh)          # alt golge = 3B

def draw_keyboard(d, x, y, w, h, pressed):
    """Yukaridan bakisli modern klavye. Sik tus izgarasi + genis renkli Enter (sagda) +
    aksan tuslar + spacebar. pressed: basili tus indeksleri kumesi."""
    # kasa (yuvarlak koseli his icin koseleri kesiyoruz) + on kalinlik
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=KB_CASE)
    d.rectangle([x, y + h - 2, x + w - 1, y + h - 1], fill=KB_LIP)
    for cx, cy in [(x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)]:
        d.point((cx, cy), fill=(0, 0, 0, 0))           # koseleri sil
    # ic deck
    dx0, dy0, dx1, dy1 = x + 2, y + 1, x + w - 3, y + h - 3
    d.rectangle([dx0, dy0, dx1, dy1], fill=KB_DECK)

    rows = 4; gap = 1
    kh = (dy1 - dy0 + 1 - (rows - 1) * gap) // rows
    right_col_w = 7                                     # sag blok (Enter) genisligi
    grid_x1 = dx1 - right_col_w - 1
    idx = 0
    for r in range(rows):
        ky = dy0 + r * (kh + gap)
        ky2 = ky + kh - 1
        if r == rows - 1:
            # alt sira: spacebar (genis) + iki yan tus
            _cap(d, dx0, ky, dx0 + 5, ky2, ACCENT if 0 else CAP, CAP_SH, idx in pressed); idx += 1
            _cap(d, dx0 + 7, ky, grid_x1 - 4, ky2, CAP, CAP_SH, idx in pressed); idx += 1   # spacebar
            _cap(d, grid_x1 - 2, ky, grid_x1, ky2, CAP, CAP_SH, idx in pressed); idx += 1
        else:
            cols = 9
            gw = grid_x1 - dx0
            kw = max(2, (gw - (cols - 1)) // cols)
            for c in range(cols):
                kx = dx0 + c * (kw + 1)
                if kx + kw > grid_x1: break
                # aksan tuslar: sol-ust Esc kirmizi, birkac tus teal
                col = CAP; sh = CAP_SH
                if r == 0 and c == 0: col = ESC
                elif (r * 9 + c) in (5, 12, 20): col = ACCENT
                _cap(d, kx, ky, kx + kw - 1, ky2, col, sh, idx in pressed); idx += 1
    # sag blok: renkli genis ENTER (ust 2 sirayi kaplar) + altinda iki tus
    ex0 = grid_x1 + 2; ex1 = dx1
    e_y0 = dy0; e_y1 = dy0 + 2 * kh + gap - 1
    if (99 in pressed):
        d.rectangle([ex0, e_y0 + 1, ex1, e_y1], fill=ENTER_SH)
    else:
        d.rectangle([ex0, e_y0, ex1, e_y1 - 1], fill=ENTER)
        d.line([(ex0, e_y1), (ex1, e_y1)], fill=ENTER_SH)
    for r in (2, 3):                                    # Enter altindaki tuslar
        ky = dy0 + r * (kh + gap)
        _cap(d, ex0, ky, ex1, ky + kh - 1, CAP, CAP_SH, (90 + r) in pressed)

# ---------- animasyonlar ----------
def anim_idle(n=8):
    """Yumusak nefes: dikey squash (ayaklar sabit). Gozler SABIT (oynamaz)."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.06 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant()                                  # gozler sabit
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))       # ayaklar sabit (alt hizali)
        out.append(c)
    return out

HACK_UP = 16                               # hacking'i YUKARI kaydir: klavye alt HUD
                                           # yazilariyla cakismasin (cihaz alt bandi ARTIK 3 satir ->
                                           # mascot alani daralinca 8'den 16'ya cikarildi).
def anim_hacking(n=8):
    """clawd SABIT durur; sol/sag kollari hafif asagi-yukari; gozler hafif tarar;
    onunde klavye, tuslari basiliyor. clawd zINPLAMAZ. TUM sahne HACK_UP kadar yukarida
    (klavye alt yazilarin ustunde kalir)."""
    out = []
    kb_w = 54; kb_h = 20
    kb_x = (CANVAS - kb_w) // 2
    kb_y = CY + CH - 3 - HACK_UP           # clawd ayak hizasinda ama yukari kaydirilmis
    NK = 30                                # izgara tus sayisi (~)
    for i in range(n):
        ph = i / n * 2 * math.pi
        larm = 1 if math.sin(ph) > 0 else 0          # kollar zit fazda, ±1px
        rarm = 1 if math.sin(ph) <= 0 else 0
        eye_dx = (0, 1, 0, 0, 0, -1, 0, 0)[i % 8]
        cl = clawd_variant(eye_dx=eye_dx, larm_dy=larm, rarm_dy=rarm)
        c = base_canvas()
        place(c, cl, dy=-HACK_UP)                     # clawd yukari kaydirilmis
        d = ImageDraw.Draw(c)
        pressed = {(i * 3) % NK, (i * 5 + 4) % NK}    # her frame farkli izgara tusu
        if i % 4 == 3: pressed.add(99)                # ara sira Enter'a bas (renkli)
        draw_keyboard(d, kb_x, kb_y, kb_w, kb_h, pressed)
        out.append(c)
    return out

# --- "calisiyor" pozu DENEMESI: klavye yerine TAVADA yemek pisiren clawd ---
# Klavye ile ayni recete: clawd'in KENDI pikselleri degismez, tava tamamen
# deterministik ustluk katman ve ayni HACK_UP bandinda durur (alt HUD yazilarinin
# ustunde). Okunabilirligin sarti: (1) tava agzi ELIPS (ustten bakis) olsun, duz
# dikdortgen "tencere" gibi okunuyor; (2) sap SAGA dogru ve tavadan ACIK tonda
# cikssin (siluetin "tava" olarak taninmasini saglayan sey); (3) yumurta sari
# NOKTASI tavanin koyu icine karsi en parlak oge olsun.
PAN_RIM   = (152, 156, 170, 255)   # agiz kenari (parlak celik) — fume zemine karsi
PAN_BODY  = (98, 102, 116, 255)    # dis govde (kenarin altindaki kalinlik)
PAN_IN    = (48, 50, 60, 255)      # ic yuzey (koyu dokum)
PAN_IN_HI = (72, 76, 90, 255)      # ic yuzeyde alt kavis parlamasi (derinlik)
HANDLE    = (128, 86, 54, 255)     # ahsap sap
HANDLE_D  = (84, 54, 34, 255)
HANDLE_HI = (168, 118, 78, 255)
EGG_W     = (247, 243, 228, 255)   # yumurta akı
EGG_SH    = (204, 198, 178, 255)
YOLK      = (250, 186, 52, 255)
YOLK_HI   = (255, 226, 130, 255)
STEAM     = (228, 234, 244, 255)   # sicak turuncu govde uzerinde de beyaz kalmali
SIZZLE    = (255, 214, 140, 255)   # cizirti kivilcimi

PAN_W, PAN_H = 38, 13              # agiz elipsi
PAN_X = 6                          # tava sol kenari (sap saga 12px tasar -> 56 < 64)

HANDLE_X0 = PAN_X + PAN_W - 5     # sapin basladigi x (tava agzinin sag kenari)
HANDLE_L  = 16                    # sap uzunlugu -> uc 55'te biter (< 64)

def draw_pan(c, x, y):
    """Ustten bakisli tava: govde kalinligi + parlak agiz + koyu ic + saga ahsap sap.
    Sap DUMDUZ yataydir: 16px'de birkac piksel yukselen egim bu olcekte duzgun bir
    cizgi degil BASAMAK uretiyor ve sap yamuk/kirik gorunuyor. Duz sapin tavaya
    karismasini onleyen sey egim degil, ahsap rengi + ust parlama/alt golge cifti."""
    d = ImageDraw.Draw(c)
    d.ellipse([x, y + 3, x + PAN_W - 1, y + PAN_H - 1 + 3], fill=PAN_BODY)   # kalinlik
    d.ellipse([x, y, x + PAN_W - 1, y + PAN_H - 1], fill=PAN_RIM)            # agiz kenari
    d.ellipse([x + 2, y + 2, x + PAN_W - 3, y + PAN_H - 3], fill=PAN_IN)     # ic yuzey
    d.arc([x + 3, y + 3, x + PAN_W - 4, y + PAN_H - 3], 25, 155, fill=PAN_IN_HI)
    ym = y + PAN_H // 2 - 1
    hx0, hx1 = x + PAN_W - 5, x + PAN_W - 5 + HANDLE_L - 1
    d.rectangle([hx0, ym, hx1, ym + 2], fill=HANDLE)
    d.line([(hx0, ym), (hx1, ym)], fill=HANDLE_HI)          # ust kenar parlamasi
    d.line([(hx0, ym + 3), (hx1, ym + 3)], fill=HANDLE_D)   # alt golge (kalinlik)
    # tavaya baglanan bilezik METAL (koyu ahsapla yapilirsa sapin dibinde kirli bir
    # blok olarak okunuyor); sap ucunda acik tonlu topuz.
    d.rectangle([hx0, ym - 1, hx0 + 1, ym + 3], fill=PAN_BODY)
    d.line([(hx0, ym - 1), (hx0 + 1, ym - 1)], fill=PAN_RIM)
    d.rectangle([hx1 - 1, ym, hx1, ym + 2], fill=HANDLE_HI)      # sap ucu (topuz)

def draw_egg(c, cx, cy, flat=False):
    """Sahanda yumurta: ak (alt golgeli) + parlak sari. flat=True -> havada hafif ezik."""
    d = ImageDraw.Draw(c)
    h = 7 if not flat else 6
    d.ellipse([cx - 8, cy - h // 2 + 1, cx + 7, cy + h // 2 + 1], fill=EGG_SH)  # alt golge
    d.ellipse([cx - 8, cy - h // 2, cx + 7, cy + h // 2], fill=EGG_W)
    d.ellipse([cx - 10, cy - 2, cx - 5, cy + 2], fill=EGG_W)                   # akin sol lobu
    d.ellipse([cx - 2, cy - 3, cx + 3, cy + 1], fill=YOLK)
    d.point((cx - 1, cy - 2), fill=YOLK_HI)

def draw_steam(c, x, y, alpha, wob=0.0):
    """Yukari suzulen buhar SERIDI: 2px genis, sinuslu kivrilan, tepeye dogru sonen.
    Tek tek pikselden olusan kucuk glifler bu boyutta "s harfi" gibi okunuyordu —
    surekli serit + dikey alpha rampasi buhar okumasini veren sey."""
    p = c.load()
    r, g, b, _ = STEAM
    H = 8
    for k in range(H):
        dx = int(round(1.4 * math.sin(k * 0.85 + wob)))
        a = alpha * (H - k) // H                      # tepeye dogru erir
        if a <= 12: continue
        for sx in (x + dx, x + dx + 1):
            if 0 <= sx < CANVAS and 0 <= y - k < CANVAS:
                p[sx, y - k] = (r, g, b, a)

def anim_cooking(n=8):
    """clawd tavada yemek ceviriyor: sag kol sapi tutar, tava dip-yap ve yumurta
    havaya sicrar (2. -4. frame), gozler yukari yumurtayi takip eder; tavadan
    buhar suzulur, yumurta tavadayken cizirti kivilcimlari. HACK_UP bandinda."""
    out = []
    # Tava agzi clawd'in AYAKLARINI yarilar (klavyedeki gibi "arkasinda duruyor"
    # okumasi): daha asagida birakilirsa bacaklar tavayla govde arasinda bosta asili
    # kaliyor ve sahne "tava yere dusmus" gibi gorunuyor.
    pan_y0 = CY + CH - 3 - HACK_UP - 2
    pan_dy = (0, 1, -2, -1, 0, 1, 0, 0)          # savurma: dip -> yukari itis -> yakalama
    egg_dy = (0, 1, -3, -9, -6, 1, 0, 0)
    # buhar: (x, faz, kivrim) — tava agzi boyunca dagitilmis uc serit
    steam = ((13, 0, 0.0), (30, 3, 1.7), (48, 5, 3.2))
    life = 5
    for i in range(n):
        pdy = pan_dy[i % 8]
        air = i % 8 in (2, 3, 4)                 # yumurta havada
        cl = clawd_variant(eye_dy=-1 if air else 0,
                           larm_dy=(0, 0, -1, -1, 0, 0, 0, 0)[i % 8],
                           rarm_dy=max(-1, min(1, pdy)))    # sag kol sapla birlikte
        c = base_canvas()
        place(c, cl, dy=-HACK_UP)
        py = pan_y0 + pdy
        draw_pan(c, PAN_X, py)
        # on kol: sag kolun ALT kenarindan (sprite y14 -> canvas) sapa iner. Kol ve
        # tava ayni yonde hareket ettigi icin (rarm_dy = pdy) kavrama hic kopmaz.
        # buhar YUMURTADAN ONCE: havadaki yumurtanin ustune denk gelen serit onde
        # kalirsa yumurtaya bulasmis bir leke gibi okunuyor.
        for (sx, ph, wob) in steam:
            age = (i - ph) % n
            if age >= life: continue
            draw_steam(c, sx, py - 1 - age * 2, 230 - int(170 * age / life), wob + age * 0.5)
        ecx = PAN_X + PAN_W // 2 + 1                 # akin sol lobu tasiyor -> hafif saga
        draw_egg(c, ecx, py + PAN_H // 2 + egg_dy[i % 8], flat=air)
        if not air:                              # tavadayken cizirdar
            p = c.load()
            for (sx, sy) in ((ecx - 10, py + 3), (ecx + 9, py + 7)) if i % 2 == 0 else \
                            ((ecx + 10, py + 3), (ecx - 9, py + 8)):
                if 0 <= sx < CANVAS and 0 <= sy < CANVAS: p[sx, sy] = SIZZLE
        out.append(c)
    return out

# --- dusunce balonu (3B, golgeli puffy bulut) ---
CLOUD_W = (247, 248, 246, 255)   # ust beyaz
CLOUD_SH = (196, 199, 212, 255)  # alt golge (hacim)
CLOUD_LN = (62, 64, 80, 255)     # outline
CLOUD_HI = (255, 255, 255, 255)  # ust-sol highlight
DOT = (70, 72, 88, 255)

def _shade_from_mask(mask):
    """Bir maskeden 3B sprite: turetilmis temiz outline + alt golge (hacim) + ust-sol highlight."""
    w, h = mask.size; mp = mask.load()
    spr = Image.new("RGBA", (w, h), (0, 0, 0, 0)); p = spr.load()
    def solid(x, y): return 0 <= x < w and 0 <= y < h and mp[x, y] >= 128
    for y in range(h):
        for x in range(w):
            if not solid(x, y): continue
            if not (solid(x-1, y) and solid(x+1, y) and solid(x, y-1) and solid(x, y+1)):
                p[x, y] = CLOUD_LN                       # kenar -> outline
            elif not solid(x, y+2):                      # alta yakin ic -> golge
                p[x, y] = CLOUD_SH
            elif not solid(x-1, y-1) and y < h * 0.5:    # ust-sol ic rim -> highlight
                p[x, y] = CLOUD_HI
            else:
                p[x, y] = CLOUD_W
    return spr

def _build_rrect(w, h, r):
    """KARE/bloklu balon (hafif yuvarlatilmis kose, clawd'in koseli diliyle uyumlu)."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    return _shade_from_mask(m)

def _build_ellipse(w, h):
    """Yuvarlak minik puf (iz kabarcigi icin)."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).ellipse([0, 0, w - 1, h - 1], fill=255)
    return _shade_from_mask(m)

# ana kare balon + iki iz kabarcigi (ilk cikan minik puf YUVARLAK)
_BUBBLE = _build_rrect(22, 12, 3)
_BUB1   = _build_rrect(5, 4, 1)
_BUB2   = _build_ellipse(4, 4)

def draw_thought_bubble(c, ndots):
    # TAMAMEN clawd ustunde (kafayi kapatmaz): ana balon sag-ust, iz kabarciklari yukari
    c.alpha_composite(_BUBBLE, (38, 0))                  # x38-59, y0-11 (clawd y17 ustunde)
    c.alpha_composite(_BUB1, (34, 11))                   # x34-38, y11-14
    c.alpha_composite(_BUB2, (30, 14))                   # yuvarlak minik puf (kafaya en yakin)
    d = ImageDraw.Draw(c)
    for k in range(ndots):                               # ... noktalari
        dx = 43 + k * 4
        d.rectangle([dx, 5, dx + 1, 6], fill=DOT)

def anim_think(n=8):
    """Dusunen clawd: sakin, hafif yana sallanir; gozler yukari bakip gezinir;
    bas ustundeki dusunce balonunda ... noktalari sirayla dolar."""
    out = []
    for i in range(n):
        dx = round(1.5 * math.sin(i / n * 2 * math.pi))          # hafif yana sallanma ±1
        eye_dx = (0, 0, 1, 1, 0, -1, -1, 0)[i % 8]               # yavas gezinme
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=-1)             # gozler yukari + gezinir
        c = base_canvas()
        place(c, cl, dx=dx)
        ndots = (i // 2) % 3 + 1                                  # 1..3 dolar (dongu)
        draw_thought_bubble(c, ndots)
        out.append(c)
    return out

def anim_happy(n=8):
    """Sevincli: clawd ziplar (2 hop), kollar havada kalkar, 4 ayak zit fazda sag-sol
    mekik yapar, gozler > < , cevrede kalpler cikip kaybolur."""
    out = []
    # kalp olaylari: (x, taban_y, baslangic_frame) — age<life iken yukari suzulur
    hearts = [(3, 26, 0), (55, 30, 2), (29, 3, 4), (2, 12, 5), (55, 14, 6)]
    life = 4
    for i in range(n):
        bounce = round(4 * abs(math.sin(i / n * 2 * math.pi)))   # 0..4, iki hop
        airborne = bounce >= 3
        arm = -1 if airborne else 0                               # havada kollar kalkar
        # ayaklar marS: sol cift kalkar / iner, sag cift zit fazda (temiz dikey)
        lleg = -1 if i % 2 == 0 else 0
        rleg = 0 if i % 2 == 0 else -1
        cl = clawd_variant(eyes="normal", larm_dy=arm, rarm_dy=arm,   # kare gozler (eski)
                           lleg_dy=lleg, rleg_dy=rleg)
        c = base_canvas()
        place(c, cl, dy=-bounce)                                  # zipla (yukari)
        for (hx, hy, st) in hearts:                               # kalpler
            age = (i - st) % n
            if age < life: draw_heart(c, hx, hy - age)
        out.append(c)
    return out

# --- oops ogeleri: kirmizi ! + mavi ter damlasi ---
EXCL = (226, 44, 48, 255); EXCL_SH = (150, 26, 30, 255)
SWEAT = (96, 176, 228, 255); SWEAT_HI = (205, 234, 255, 255)
def draw_exclaim(c, big):
    """Bas ustunde (krem zeminde) kirmizi bold '!'. big -> hafif buyur (vurgu)."""
    d = ImageDraw.Draw(c)
    top = 2 if big else 3
    bx = 30
    d.rectangle([bx, top, bx + 2, 9], fill=EXCL)          # cubuk
    d.rectangle([bx, 11, bx + 2, 12], fill=EXCL)          # nokta
    d.rectangle([bx + 2, top, bx + 2, 12], fill=EXCL_SH)  # sag golge (3B)
def draw_sweat(c, i):
    """Sag yanaktan dokulen mavi ter damlasi (dongude tekrar)."""
    p = c.load()
    sy = 16 + (i % 4) * 3                                  # asagi damlar
    sx = 47
    drop = [(1,0),(0,1),(1,1),(2,1),(0,2),(1,2),(2,2),(1,3)]   # damla sekli
    for (dx, dy) in drop:
        x, y = sx + dx, sy + dy
        if 0 <= x < CANVAS and 0 <= y < CANVAS: p[x, y] = SWEAT
    if 0 <= sx+1 < CANVAS and 0 <= sy < CANVAS: p[sx+1, sy] = SWEAT_HI  # ust parlak

def anim_oops(n=8):
    """Eyvah: clawd hizli titrer (yatay), bas ustunde kirmizi ! (nabiz gibi),
    sag yanaktan mavi ter damlasi dokulur."""
    out = []
    shake = [-2, 2, -2, 1, -1, 2, -2, 1]
    for i in range(n):
        cl = clawd_variant()                              # govde sabit; titreme yerlesimde
        c = base_canvas()
        place(c, cl, dx=shake[i % len(shake)])
        draw_exclaim(c, big=(i % 2 == 0))
        draw_sweat(c, i)
        out.append(c)
    return out

# --- uyku: sadece clawd (kapali gozler + nefes); zzZZ YOK (kullanici istegi) ---
def anim_sleep(n=8):
    """Uyuyan clawd: idle nefesi + KAPALI cizgi gozler. Ustunde ek sembol yok."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.06 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))   # ayni nefes
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant(eyes="sleep")
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))     # ayaklar sabit (alt hizali)
        out.append(c)
    return out

# --- ask: clawd sana SORU soruyor -> ustunde sik, parlak/golgeli "?" (3B) ---
# clawd blocky diliyle uyumlu, altin/amber cam gibi. Ince strok oldugu icin
# surround-shader yerine drop-shadow + ust-sol shine (kucuk glyph'te en temizi).
QBODY = (252, 206, 92, 255); QHI = (255, 246, 206, 255)
QSH   = (176, 118, 40, 255); QLN = (92, 60, 24, 255)
_QM = [(2,0),(3,0),(4,0),(5,0),(6,0),          # ust yay
       (1,1),(2,1),(6,1),(7,1),
       (1,2),(2,2),(6,2),(7,2),
       (6,3),(7,3),                              # sag kenar asagi
       (5,4),(6,4),
       (4,5),(5,5),
       (3,6),(4,6),                              # capraz -> govde
       (3,7),(4,7),
       (3,8),(4,8),                              # govde alt
       (3,10),(4,10),                            # nokta
       (3,11),(4,11)]
_QMset = set(_QM)
def _build_qmark():
    w, h = 10, 13
    spr = Image.new("RGBA", (w, h), (0, 0, 0, 0)); p = spr.load()
    for (x, y) in _QM:                            # 1) drop shadow (sag-alt)
        if (x+1, y+1) not in _QMset and 0 <= x+1 < w and 0 <= y+1 < h:
            p[x+1, y+1] = QLN if ((x+1, y+2) not in _QMset) else QSH
    for (x, y) in _QM: p[x, y] = QBODY            # 2) govde
    for (x, y) in _QM:                            # 3) ust-sol shine
        if (x, y-1) not in _QMset and (x-1, y) not in _QMset:
            p[x, y] = QHI
    return spr
_QMARK = _build_qmark()

def anim_ask(n=8):
    """clawd sana soru soruyor: sakin durur, gozler yukari '?'ye bakar; ustundeki
    parlak '?' yumusakca inip cikar (nabiz gibi hafif). Interrupt DEGIL — Claude
    AskUserQuestion/ExitPlanMode ile SENI bekliyor."""
    out = []
    for i in range(n):
        ph = i / n * 2 * math.pi
        dx = round(1.0 * math.sin(ph))                       # cok hafif sallanma
        eye_dx = (0, 0, 1, 1, 0, -1, -1, 0)[i % 8]
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=-1)         # yukari bakar
        c = base_canvas()
        place(c, cl, dx=dx)
        bob = round(1.5 * math.sin(ph))                      # -1.5..1.5 yumusak bob
        c.alpha_composite(_QMARK, (27, 3 - bob))             # bas ustunde ortali
        out.append(c)
    return out

# --- agents: clawd alt-agent'lari yonetir -> YUKARI kayar, ASAGIDAKI mini clawd'lara bakar ---
# Ayri, sakin bir "gozetim" pozu (klavye YOK). clawd yukari kaydirilir ki ekranin
# altinda gercek bir ZEMIN bandi acilsin; firmware o zemine + iki yana mini clawd'lar
# kondurur. Cihaz bu anim'i yalniz UST satirlari (pushH=40) cizer -> alt bant mini'lere kalir.
AGENTS_UP = 16                                  # clawd'i kac px yukari cek (main.cpp pushH ile uyumlu;
                                                 # HUD 3 satira cikinca mini sirasina yer acmak icin 8->16)
def anim_agents(n=8):
    """clawd alt-ajanlarini gozetir: yukari kayip ASAGI kucuk clawd'lara bakar
    (gozler asagi + hafif saga-sola tarama), sakin nefes alir. Klavye yok."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.05 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))   # yumusak nefes
        nh = max(1, int(round(CH * sy)))
        eye_dx = (0, 1, 1, 0, -1, -1, 0, 0)[i % 8]                       # asagidaki mini'leri tarar
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=3)                       # gozler ASAGI bakar
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, (CY - AGENTS_UP) + (CH - nh)))        # yukari kaydir, ayak sabit
        out.append(c)
    return out

# --- love: OKSAMA (surtme jesti) -> mutlu > < gozler + yukselen PixelLab kalpleri ---
# Sakin/sevecen his: happy'den FARKLI (ziplama yok, kutlama degil sefkat). clawd
# minik sag-sol salinir + cok hafif nefes; cevrede PixelLab kalpleri suzulup solar.
def anim_love(n=8):
    """Oksama: clawd mutlu > < gozlerle sakin durur (minik salinim + hafif nefes,
    ZIPLAMA yok); cevresinde PixelLab kalpleri yukselip solar."""
    out = []
    # (x, taban_y, baslangic_frame, hedef_yukseklik_px) — cesitli boy = derinlik
    hearts = [(15, 30, 0, 15), (49, 33, 2, 13), (32, 8, 3, 16),
              (7, 21, 4, 11), (57, 19, 6, 12)]
    life = 5
    for i in range(n):
        ph = i / n * 2 * math.pi
        dx = round(1.0 * math.sin(ph))                       # minik sag-sol salinim
        breath = 1 if i % 4 in (1, 2) else 0                 # cok hafif nefes (kollar)
        cl = clawd_variant(eyes="happy", larm_dy=-breath, rarm_dy=-breath)
        c = base_canvas()
        place(c, cl, dx=dx)
        for (hx, hy, st, th) in hearts:                      # yukselen kalpler
            age = (i - st) % n
            if age < life:
                # HEP opak: koyu zeminde alpha dusunce kalp grilesiyor. Fade yerine
                # kalp yukselir ve life sonunda ANINDA kaybolur (temiz, hep canli renk).
                draw_heart_spr(c, hx, hy - age * 3, th=th, alpha=255)
        out.append(c)
    return out

# --- beyin: PixelLab (08_brain.py) — baglam-doluluk gostergesi. clawd deterministik
# kalir; PixelLab yalniz "beyin" elementini uretir (hibrit recete, kalp ile ayni yontem).
def _load_brain_spr():
    hp = os.path.join(HERE, "out/brain.png")
    if not os.path.exists(hp): return None
    im = Image.open(hp).convert("RGBA"); px = im.load(); w, h = im.size
    bg = px[0, 0][:3]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if all(abs((r, g, b)[k] - bg[k]) <= 45 for k in range(3)):
                px[x, y] = (0, 0, 0, 0)
    bb = im.getchannel("A").getbbox()
    return im.crop(bb) if bb else None
BRAIN_SPR = _load_brain_spr()

def _brain_at(th):
    """BRAIN_SPR'yi th px yuksekliginde, oranli genislikte olcekle."""
    w, h = BRAIN_SPR.size
    tw = max(1, round(w * th / h))
    return BRAIN_SPR.resize((tw, th), Image.NEAREST)

# HEPSI AYNI sicak kirmizi-amber aile (gok kusagi gibi gorunmesin diye — eskiden
# bos=mor-gri + yuzey=krem-sari + dolum=kirmizi/amber FARKLI ailelerdi, bir arada
# gogun kusagi gibi okunuyordu). Simdi tek ailede: bos=sonuk/soluk, dolu=canli/doygun.
BRAIN_LOW     = (120, 96, 92, 255)    # bos/"cam" hissi (soluk, ayni sicak aile — SADECE gri degil)
BRAIN_FULL_LO = (196, 40, 44, 255)    # sivi taban (koyu kirmizi)
BRAIN_FULL_HI = (250, 150, 64, 255)   # sivi yuzeye yakin (amber)
BRAIN_SURF    = (255, 196, 132, 255)  # parlak yuzey cizgisi (amber'in acik tonu, SARI/krem DEGIL)

def _brain_shade_map(spr):
    """Sprite'in KENDI kivrim golgelerini (koyu/acik pembe cizgiler) parlaklik orani
    olarak cikar (ortalamaya gore normalize) — dolum rengi bu oranla carpilinca
    kivrimlar HER doluluk seviyesinde gorunur kalir (aksi halde duz sivi rengi
    beyin dokusunu tamamen ortup 'ne oldugu belirsiz bir yuvarlak'a donusturuyordu)."""
    w, h = spr.size; sp = spr.load()
    lums = [sum(sp[x, y][:3]) for y in range(h) for x in range(w) if sp[x, y][3] > 0]
    avg = (sum(lums) / len(lums)) if lums else 384.0
    shade = {}
    for y in range(h):
        for x in range(w):
            r, g, b, a = sp[x, y]
            if a == 0: continue
            shade[(x, y)] = min(1.3, max(0.55, sum((r, g, b)) / avg))
    return shade

def draw_brain_gauge(c, cx, cy, th, fill):
    """(cx,cy) MERKEZLI beyin; th px yukseklik; fill 0..1 doluluk orani — BARDAK GIBI
    asagidan yukari dolar. Bos kisim sonuk/cam hissi, dolu kisim sicak kirmizi->amber
    sivi + parlak yuzey cizgisi (context doluyor gostergesi). Kivrim dokusu (shade map)
    HER katmanda korunur ki beyin her doluluk seviyesinde 'beyin gibi' okunsun."""
    if BRAIN_SPR is None: return
    spr = _brain_at(th); sp = spr.load(); w, h = spr.size
    shade = _brain_shade_map(spr)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0)); op = out.load()
    level_y = h - round(fill * h)                    # bu satirdan asagisi "dolu"
    for y in range(h):
        for x in range(w):
            a = sp[x, y][3]
            if a == 0: continue
            sh = shade[(x, y)]
            if y < level_y - 1:
                base = BRAIN_LOW
            elif y <= level_y:
                base = BRAIN_SURF
            else:
                t = (y - level_y) / max(1, h - level_y)
                base = tuple(BRAIN_FULL_HI[k] * (1 - t) + BRAIN_FULL_LO[k] * t for k in range(3))
            col = tuple(min(255, round(base[k] * sh)) for k in range(3))
            op[x, y] = (*col, a)
    c.alpha_composite(out, (cx - w // 2, cy - h // 2))

def draw_brain_plain(c, cx, cy, th):
    """Beyni PixelLab'in KENDI renkleriyle (dolum efekti yok) (cx,cy) merkezli ciz."""
    if BRAIN_SPR is None: return
    spr = _brain_at(th)
    c.alpha_composite(spr, (cx - spr.size[0] // 2, cy - spr.size[1] // 2))

BRAIN_CX, BRAIN_CY, BRAIN_TH = 32, 8, 15    # kafa ustunde ortali (clawd CY=17'nin hemen ustu)

def anim_brain_full(n=16):
    """Context doluyor: clawd sakin nefes alir, bas ustundeki beyin BARDAK GIBI yavas
    yavas dolar, full olunca tekrar bosalir — dongu tekrarlanir; STRES isareti olarak
    yanaktan (oops'taki gibi) ter damlasi surekli dokulur. DIM/SLEEP'e gecmeden ONCE,
    aktif-bostayken context kritik seviyedeyken gosterilir."""
    out = []
    for i in range(n):
        ph = i / n * 2 * math.pi
        fill = 0.5 - 0.5 * math.cos(ph)                       # 0 -> 1 -> 0 tek dongude
        sy = 1.0 - 0.05 * (0.5 - 0.5 * math.cos(ph))          # yumusak nefes
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant()
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))
        draw_brain_gauge(c, BRAIN_CX, BRAIN_CY, BRAIN_TH, fill)
        draw_sweat(c, i)                                       # stres: ter damlasi (oops ile ayni cizim)
        out.append(c)
    return out

# --- compact: PreCompact -> beyin gorunur (dolum yok, sakin/dogal renk) + ustunde
# minik yildizlar bagimsiz fazlarla yanip soner ("temizleniyor" hissi). clawd gozleri
# KAPALI (sleep gozleri, huzurlu "zihni durulama" pozu) ---
STAR_COL = (255, 236, 158, 255)
_STAR_PX = [(1, 0), (0, 1), (1, 1), (2, 1), (1, 2)]     # minik elmas/artı yildiz
def draw_star(c, cx, cy, bright):
    if bright <= 0: return
    a = max(0, min(255, round(255 * bright)))
    spr = Image.new("RGBA", (3, 3), (0, 0, 0, 0)); sp = spr.load()
    for (dx, dy) in _STAR_PX: sp[dx, dy] = (*STAR_COL[:3], a)
    c.alpha_composite(spr, (cx - 1, cy - 1))

# (x, y, faz) — bagimsiz sinus fazlariyla sirayla yanip soner (hepsi ayni anda degil)
STAR_DEFS = [(16, 3, 0.0), (48, 4, 0.6), (20, 15, 1.3), (44, 16, 2.1), (32, -1, 2.8)]
STAR_DEFS = [(x, max(0, y), ph) for (x, y, ph) in STAR_DEFS]

def anim_compact(n=12):
    """Compact calisiyor: beyin gorunur (dogal renk, dolum YOK), clawd huzurlu (gozler
    kapali) nefes alir, ustunde minik yildizlar tek tek yanip soner (zihin temizleniyor)."""
    out = []
    for i in range(n):
        ph = i / n * 2 * math.pi
        sy = 1.0 - 0.04 * (0.5 - 0.5 * math.cos(ph))
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant(eyes="sleep")
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))
        draw_brain_plain(c, BRAIN_CX, BRAIN_CY, BRAIN_TH)
        for (sx, sy2, sph) in STAR_DEFS:
            b = math.sin(ph + sph)
            if b > 0: draw_star(c, sx, sy2, b)
        out.append(c)
    return out

# --- tickle: CIFT-DOKUNUS -> gidiklanma. clawd hizli saga-sola titrer (kikirder),
# gozler > < ; ustte sembol yok. oops'un yatay titremesiyle ayni teknik, mutlu goz. ---
def anim_tickle(n=8):
    """Gidiklanma: clawd hizli/genis saga-sola titrer + minik dikey zipirti
    (kikirdama), gozler > < . Cift-dokunusla tetiklenir, transient."""
    out = []
    shake = [-3, 3, -2, 3, -3, 2, -3, 2]                     # oops'tan genis/hizli
    for i in range(n):
        wig = -1 if i % 2 == 0 else 0                        # minik dikey kikirdama
        cl = clawd_variant(eyes="happy", larm_dy=wig, rarm_dy=wig)
        c = base_canvas()
        place(c, cl, dx=shake[i % len(shake)], dy=wig)
        out.append(c)
    return out

# --- idle havuzu #2: KULAK USTU KULAKLIK ile muzik dinleyen clawd ---
# "Dinlenme" pozu tek maskot olmasin diye ikinci bir sakin poz. clawd'in KENDI
# pikselleri DEGISMEZ (kimlik korunur) — kulaklik tamamen deterministik ustluk
# katman, klavye/dusunce-balonu ile ayni recete (PixelLab GEREKMEZ).
HP_DARK = (46, 44, 62, 255)     # kulaklik govdesi (koyu, klavye kasasiyla ayni aile)
HP_MID  = (86, 84, 112, 255)    # kulak yastigi
HP_HI   = (168, 166, 200, 255)  # ust-sol parlama (3B his)
HP_ACC  = (232, 120, 70, 255)   # kap ortasindaki minik "driver" vurgusu (clawd turuncusu)
HP_EDGE = (22, 20, 32, 255)     # cok koyu dis hat: sicak turuncu govdeden ayirir
NOTE_A  = (120, 200, 190, 255)  # nota renkleri (teal / amber, donusumlu)
NOTE_B  = (240, 190, 96, 255)

# --- nota glifleri: TEK 8'lik (sap+bayrak+govde) ve KIRISLI cift 8'lik (♫) ---
# Kucuk boyutta okunabilirlik icin govde DOLU ve saptan belirgin genis; iki farkli
# glif kullanmak "muzik" okumasini guclendirir (tek sembol tekrarindan iyi).
_NOTE1 = {  # 7x9  ♪
    "px": [(4,0),(4,1),(4,2),(4,3),(4,4),(4,5),(4,6),                 # sap
           (5,0),(6,1),(6,2),(5,3),                                   # bayrak (kivrimli)
           (1,6),(2,6),(3,6),
           (0,7),(1,7),(2,7),(3,7),
           (1,8),(2,8),(3,8)],                                        # dolu govde
    "hi": [(1,7)], "w": 7, "h": 9,
}
_NOTE2 = {  # 8x8  ♫ — govdeler saplarin SOLUNDA (klasik dizilim), kiris ustte
    "px": [(2,0),(3,0),(4,0),(5,0),(6,0),(7,0),                       # kiris (beam)
           (2,1),(3,1),(4,1),(5,1),(6,1),(7,1),
           (2,2),(2,3),(2,4),                                         # sol sap
           (7,2),(7,3),(7,4),                                         # sag sap
           (0,5),(1,5),(2,5),(0,6),(1,6),(2,6),(1,7),(2,7),           # sol govde (yuvarlak)
           (5,5),(6,5),(7,5),(5,6),(6,6),(7,6),(6,7),(7,7)],          # sag govde
    "hi": [(0,5),(5,5)], "w": 8, "h": 8,
}

def draw_note(c, x, y, col, alpha=255, glyph=0):
    """Nota glifini (x,y) sol-ust kosesinden, alpha ile ciz. glyph 0=♪, 1=♫."""
    g = (_NOTE1, _NOTE2)[glyph % 2]
    spr = Image.new("RGBA", (g["w"], g["h"]), (0, 0, 0, 0)); p = spr.load()
    r, gg, b, _ = col
    for (nx, ny) in g["px"]: p[nx, ny] = (r, gg, b, alpha)
    for (nx, ny) in g["hi"]:                                # ic parlama = hacim
        p[nx, ny] = (min(255, r + 60), min(255, gg + 60), min(255, b + 60), alpha)
    c.alpha_composite(spr, (x, y))

def draw_headphones(c, dx=0, dy=0):
    """Kulak USTU kulaklik. Okunabilirligin uc sarti (kucuk piksel-artta):
      1) kemer kafadan AYRIK gecsin (arada bosluk gorunsun) — yapisik olursa
         "sapka/sac bandi" gibi okunur,
      2) kulak kaplari BUYUK ve dikey (kulak ustu his) + ic yastik ayri tonda,
      3) kemer ile kap arasinda kisa DIKEY baglanti (askı) olsun — bu detay
         siluetin "kulaklik" olarak taninmasini saglayan sey.
    Koordinatlar clawd-yerel (44x30) -> canvas'a (CX+dx, CY+dy) ile tasinir; boylece
    kulaklik maskotun bob/lean hareketiyle BIRLIKTE kayar (kaymaz/ayrilmaz)."""
    p = c.load()
    def put(lx, ly, col):
        x, y = CX + dx + lx, CY + dy + ly
        if 0 <= x < CANVAS and 0 <= y < CANVAS: p[x, y] = col

    # Kaplar govdeye BITISIK olmali (arada 1px bosluk kalirsa "havada duran kutu"
    # gibi okunur): govde x7..36 -> sol kap 1..6, sag kap 37..42 (6 genis).
    CUP_L, CUP_R = 1, 37
    CUP_W = 6
    # Kaplar GOZ hizasina otursun (gozler ly3..6) ama kollarin y7 bandina girmesin:
    CUP_Y0, CUP_H = -1, 8
    ARM_L, ARM_R = CUP_L + 2, CUP_R + 3   # askilarin x'i (kap ortasi)

    # --- kemer: kafanin USTUNDEN AYRIK gecen INCE kavis (2px) ---
    # Kemer FUME ZEMIN uzerinde durur: koyu tonla cizilirse (HP_DARK 46,44,62 vs
    # zemin 36,39,44) zemine karisip kaybolur — bu yuzden kemer ACIK tonlarla
    # (HP_HI ust + HP_MID alt) cizilir, koyu ton yalniz govde uzerindeki parcalarda
    # kullanilir. Kalinlik 3 -> 2 px (3px "kask kayisi" gibi agir duruyordu).
    for lx in range(ARM_L, ARM_R + 1):
        t = (lx - (ARM_L + ARM_R) / 2) / ((ARM_R - ARM_L) / 2)
        ly = round(-4 - 6 * (1 - t * t))            # uclarda -4, ortada -10
        put(lx, ly, HP_HI)                          # ust: parlak kenar
        put(lx, ly + 1, HP_MID)                     # alt: govde tonu

    # --- askilar: kemer ucundan kabin tepesine kisa dikey bag (yine acik ton) ---
    for lx in (ARM_L, ARM_R):
        for ly in range(-4, CUP_Y0 + 1):
            put(lx, ly, HP_MID); put(lx + 1, ly, HP_DARK)

    # --- kulak kaplari ---
    # Kap iki FARKLI zemine bakiyor: ic yaridan clawd'in SICAK turuncusu, dis
    # yaridan KOYU fume zemin. Tek tonla ikisine birden ayrisamaz -> cerceve ACIK
    # (HP_MID: zemine karsi gorunur), ic dolgu KOYU (HP_DARK: turuncuya karsi
    # ayrisir), ortada tek sicak vurgu (turuncu "driver"). Onceki surumde cerceve
    # cok koyuydu (HP_EDGE) ve dis kenar zemine karisiyordu.
    for x0 in (CUP_L, CUP_R):
        for ly in range(CUP_Y0, CUP_Y0 + CUP_H):
            edge_row = ly in (CUP_Y0, CUP_Y0 + CUP_H - 1)
            for lx in range(x0, x0 + CUP_W):
                if edge_row and lx in (x0, x0 + CUP_W - 1): continue   # yuvarlatilmis kose
                rim = (edge_row or lx in (x0, x0 + CUP_W - 1))
                put(lx, ly, HP_MID if rim else HP_DARK)
        put(x0 + 1, CUP_Y0 + 1, HP_HI)                                # ust-sol parlama
        cy = CUP_Y0 + CUP_H // 2                                       # ortada driver noktasi
        put(x0 + 2, cy, HP_ACC); put(x0 + 3, cy, HP_ACC)
        put(x0 + 2, cy + 1, HP_ACC); put(x0 + 3, cy + 1, HP_ACC)

def anim_idle_music(n=8):
    """Kulaklikla muzik dinleyen clawd: > < mutlu gozler, ritme uyan hafif kafa
    sallama (2 vurus) + yana yaslanma, kollar sirayla tempo tutar, iki yandan
    yukari suzulen notalar. Sakin bir DINLENME pozu (idle havuzunun 2. uyesi)."""
    out = []
    # notalar: (baslangic_x, taban_y, faz, glif) — omur boyunca yukari suzulup soner.
    # Sag/sol dengeli, clawd'in KOL bandini (canvas x10..16 / 47..53) kesmez.
    notes = [(54, 30, 0, 0), (1, 26, 3, 1), (55, 18, 5, 1)]
    life = 5
    for i in range(n):
        beat = i % 4
        dy   = -1 if beat in (1, 2) else 0                      # ritim: 2 vurus/dongu
        dx   = (0, 0, 1, 1, 0, 0, -1, -1)[i % 8]                # hafif yana yaslanma
        larm = -1 if beat < 2 else 0                            # kollar sirayla tempo tutar
        rarm = 0 if beat < 2 else -1
        cl = clawd_variant(eyes="happy", larm_dy=larm, rarm_dy=rarm)
        c = base_canvas()
        place(c, cl, dx=dx, dy=dy)
        draw_headphones(c, dx=dx, dy=dy)                        # kulaklik maskotla BIRLIKTE kayar
        for k, (nx, ny, ph, gl) in enumerate(notes):
            age = (i - ph) % n
            if age >= life: continue
            a = 255 - int(190 * age / life)                     # yukseldikce soner
            draw_note(c, nx + (1 if age % 2 else 0), ny - age * 3,
                      NOTE_A if k % 2 == 0 else NOTE_B, a, gl)
        out.append(c)
    return out

ANIMS = {"idle": anim_idle, "idle_music": anim_idle_music,
         "hacking": anim_hacking, "cooking": anim_cooking, "happy": anim_happy,
         "think": anim_think, "oops": anim_oops, "sleep": anim_sleep, "ask": anim_ask,
         "agents": anim_agents, "love": anim_love, "tickle": anim_tickle,
         "brain_full": anim_brain_full, "compact": anim_compact}

def save(name, frames):
    dst = os.path.join(HERE, "out", f"anim_{name}")
    shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst)
    for i, f in enumerate(frames): f.save(f"{dst}/frame_{i:02d}.png")
    s = 6; pd = 6; N = len(frames)
    mont = Image.new("RGB", (N * (CANVAS * s + pd) + pd, CANVAS * s + 2 * pd), (60, 60, 60))
    for i, f in enumerate(frames):
        bg = Image.new("RGBA", f.size, BG + (255,))
        mont.paste(Image.alpha_composite(bg, f).convert("RGB").resize((CANVAS * s, CANVAS * s), Image.NEAREST),
                   (pd + i * (CANVAS * s + pd), pd))
    mont.save(os.path.join(HERE, "out", f"{name}_montage.png"))
    print(f"{name}: {N} frame {CANVAS}x{CANVAS} -> out/anim_{name}/ , out/{name}_montage.png")

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "hacking"
    for nm in (list(ANIMS) if which == "all" else [which]):
        save(nm, ANIMS[nm]())
