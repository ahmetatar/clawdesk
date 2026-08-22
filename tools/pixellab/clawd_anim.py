#!/usr/bin/env python3
# clawd's deterministic animation module: motion written in code on top of one
# clean PixelLab character frame. clawd is the same size and position in every
# scene (fixed 64x64 canvas), pixels are never redrawn (so nothing warps), and
# moods are told apart by a distinctive element.
#
#   python3 clawd_anim.py <name>    # idle | hacking  (out/anim_<name>/ + montage)
#   python3 clawd_anim.py all
import sys, os, glob, shutil, math
from collections import Counter
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BG = (36, 39, 44)  # must match include/config.h and 03_png_to_header.py

# --- canonical clawd: the south rotation of the faithful rotate_character sprite ---
# Fixed path, falling back to the char_zip2 export.
_srcpath = os.path.join(HERE, "out/clawd_south.png")
if not os.path.exists(_srcpath):
    _srcpath = glob.glob(os.path.join(HERE, "out/char_zip2/*/rotations/south.png"))[0]
_src = Image.open(_srcpath).convert("RGBA")
CLAWD = _src.crop(_src.getchannel("A").getbbox())
CW, CH = CLAWD.size                       # 44 x 30
_px = CLAWD.load()

# Fixed layout, identical in every animation.
CANVAS = 64
CX = (CANVAS - CW) // 2                    # horizontal centre
CY = (CANVAS - CH) // 2                    # vertical centre

# clawd's structure, derived from the pixels
FACE = (214, 82, 56, 255)
_dark = [(x, y) for y in range(CH) for x in range(CW) if _px[x, y][3] > 0 and sum(_px[x, y][:3]) < 170]
EYE_COL = _px[_dark[0][0], _dark[0][1]]
EYES = _dark                                       # eye pixels
ARM_Y = range(7, 15)                               # y band of the arms
LARM = [(x, y) for y in ARM_Y for x in range(0, 7)  if _px[x, y][3] > 0]   # left arm
RARM = [(x, y) for y in ARM_Y for x in range(37, CW) if _px[x, y][3] > 0]  # right arm
# legs (y23-29): left pair x7-17, right pair x26-36
LEGS_L = [(x, y) for y in range(23, CH) for x in range(7, 18)  if _px[x, y][3] > 0]
LEGS_R = [(x, y) for y in range(23, CH) for x in range(26, 37) if _px[x, y][3] > 0]

def _shift(im, pixels, dx, dy):
    """Shift the given pixels by (dx,dy): clear the old ones, redraw in place."""
    if not (dx or dy): return
    p = im.load()
    saved = [(x, y, im.getpixel((x, y))) for (x, y) in pixels]
    for (x, y) in pixels: p[x, y] = (0, 0, 0, 0)
    for (x, y, col) in saved:
        nx, ny = x + dx, y + dy
        if 0 <= nx < CW and 0 <= ny < CH: p[nx, ny] = col

def _happy_eyes(im):
    """Replace the eyes with a thin 1px > (left) and < (right)."""
    p = im.load()
    for (x, y) in EYES: p[x, y] = FACE
    gt = [(11,3),(12,4),(12,5),(11,6)]     # ">" with the vertex on the right
    lt = [(32,3),(31,4),(31,5),(32,6)]     # "<" with the vertex on the left
    for (x, y) in gt + lt:
        if 0 <= x < CW and 0 <= y < CH: p[x, y] = EYE_COL

def _sleep_eyes(im):
    """Closed eyes: replace each square eye with a thin horizontal line."""
    p = im.load()
    for (x, y) in EYES: p[x, y] = FACE
    left  = [(x, y) for (x, y) in EYES if x < CW // 2]
    right = [(x, y) for (x, y) in EYES if x >= CW // 2]
    for cl in (left, right):
        if not cl: continue
        xs = [x for x, y in cl]; ys = [y for x, y in cl]
        cy = (min(ys) + max(ys)) // 2
        for x in range(min(xs) - 1, max(xs) + 2):        # slightly overhanging line
            if 0 <= x < CW: p[x, cy] = EYE_COL

def clawd_variant(eye_dx=0, eye_dy=0, larm_dy=0, rarm_dy=0, eyes="normal",
                  lleg_dy=0, rleg_dy=0):
    """A copy of clawd. eyes='happy' -> > < ; eyes='sleep' -> closed lines. The
    eyes shift by (eye_dx,eye_dy); arms and legs shift vertically."""
    im = CLAWD.copy(); p = im.load()
    # --- eyes ---
    if eyes == "happy":
        _happy_eyes(im)
    elif eyes == "sleep":
        _sleep_eyes(im)
    elif eye_dx or eye_dy:
        for (x, y) in EYES: p[x, y] = FACE
        for (x, y) in EYES:
            nx = min(CW - 1, max(0, x + eye_dx)); ny = min(CH - 1, max(0, y + eye_dy))
            p[nx, ny] = EYE_COL
    # --- arms and legs: vertical _shift only, so nothing warps ---
    _shift(im, LARM, 0, larm_dy)
    _shift(im, RARM, 0, rarm_dy)
    _shift(im, LEGS_L, 0, lleg_dy)
    _shift(im, LEGS_R, 0, rleg_dy)
    return im

# --- small code-drawn heart, appearing at various points on screen ---
HEART = (222, 40, 44, 255); HEART_HI = (255, 90, 80, 255)
_HEART_PX = [(1,0),(3,0),(0,1),(1,1),(2,1),(3,1),(4,1),
             (0,2),(1,2),(2,2),(3,2),(4,2),(1,3),(2,3),(3,3),(2,4)]
def draw_heart(c, cx, cy):
    p = c.load()
    for (x, y) in _HEART_PX:
        px_, py_ = cx + x, cy + y
        if 0 <= px_ < CANVAS and 0 <= py_ < CANVAS:
            p[px_, py_] = HEART
    if 0 <= cx+1 < CANVAS and 0 <= cy+1 < CANVAS: p[cx+1, cy+1] = HEART_HI  # highlight

# --- PixelLab pixel-art heart (from 07_heart.py), used by the love animation ---
# clawd stays deterministic; PixelLab only produces the heart element. It comes
# back on a flat grey background, so key it out by corner color and crop.
def _load_heart_spr():
    hp = os.path.join(HERE, "out/heart.png")
    if not os.path.exists(hp): return None
    im = Image.open(hp).convert("RGBA"); px = im.load(); w, h = im.size
    bg = px[0, 0][:3]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if all(abs((r, g, b)[k] - bg[k]) <= 45 for k in range(3)):
                px[x, y] = (0, 0, 0, 0)                      # drop the grey background
    bb = im.getchannel("A").getbbox()
    return im.crop(bb) if bb else None
HEART_SPR = _load_heart_spr()

def draw_heart_spr(c, cx, cy, th=13, alpha=255):
    """Composite the PixelLab heart centred at (cx,cy), th px tall. Falls back to
    the code-drawn heart when the sprite is missing."""
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

# ---------- keyboard (top-down, colored Enter + accent keys) ----------
KB_CASE = (40, 38, 56, 255)      # case
KB_DECK = (60, 58, 78, 255)      # deck
KB_LIP  = (30, 28, 44, 255)      # front lip
CAP     = (168, 164, 192, 255)   # keycap
CAP_SH  = (112, 108, 138, 255)   # keycap shadow (depth)
CAP_DN  = (96, 92, 120, 255)     # pressed key
ENTER   = (232, 120, 70, 255)    # Enter, in clawd orange
ENTER_SH= (170, 78, 44, 255)
ESC     = (210, 90, 96, 255)     # Esc accent
ACCENT  = (96, 180, 170, 255)    # teal accent keys

def _cap(d, x0, y0, x1, y1, col, sh, pressed):
    if pressed:
        d.rectangle([x0, y0 + 1, x1, y1], fill=CAP_DN)
    else:
        d.rectangle([x0, y0, x1, y1 - 1], fill=col)
        d.line([(x0, y1), (x1, y1)], fill=sh)          # bottom shadow = depth

def draw_keyboard(d, x, y, w, h, pressed):
    """Top-down keyboard: key grid + a wide colored Enter on the right + accent
    keys + spacebar. `pressed` is the set of pressed key indices."""
    # case (corners cut for a rounded feel) + front lip
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=KB_CASE)
    d.rectangle([x, y + h - 2, x + w - 1, y + h - 1], fill=KB_LIP)
    for cx, cy in [(x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)]:
        d.point((cx, cy), fill=(0, 0, 0, 0))           # cut the corners
    # deck
    dx0, dy0, dx1, dy1 = x + 2, y + 1, x + w - 3, y + h - 3
    d.rectangle([dx0, dy0, dx1, dy1], fill=KB_DECK)

    rows = 4; gap = 1
    kh = (dy1 - dy0 + 1 - (rows - 1) * gap) // rows
    right_col_w = 7                                     # width of the Enter block
    grid_x1 = dx1 - right_col_w - 1
    idx = 0
    for r in range(rows):
        ky = dy0 + r * (kh + gap)
        ky2 = ky + kh - 1
        if r == rows - 1:
            # bottom row: spacebar + two side keys
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
                # accents: red Esc top-left, a few teal keys
                col = CAP; sh = CAP_SH
                if r == 0 and c == 0: col = ESC
                elif (r * 9 + c) in (5, 12, 20): col = ACCENT
                _cap(d, kx, ky, kx + kw - 1, ky2, col, sh, idx in pressed); idx += 1
    # right block: the wide Enter (spanning 2 rows) plus two keys below it
    ex0 = grid_x1 + 2; ex1 = dx1
    e_y0 = dy0; e_y1 = dy0 + 2 * kh + gap - 1
    if (99 in pressed):
        d.rectangle([ex0, e_y0 + 1, ex1, e_y1], fill=ENTER_SH)
    else:
        d.rectangle([ex0, e_y0, ex1, e_y1 - 1], fill=ENTER)
        d.line([(ex0, e_y1), (ex1, e_y1)], fill=ENTER_SH)
    for r in (2, 3):                                    # keys below Enter
        ky = dy0 + r * (kh + gap)
        _cap(d, ex0, ky, ex1, ky + kh - 1, CAP, CAP_SH, (90 + r) in pressed)

# ---------- animations ----------
def anim_idle(n=8):
    """Soft breathing: a vertical squash with the feet planted. Eyes stay still."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.06 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant()
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))       # bottom-aligned
        out.append(c)
    return out

HACK_UP = 16                               # shift hacking up so the keyboard clears
                                           # the device's 3-line HUD band
def anim_hacking(n=8):
    """clawd stays put, arms moving slightly up and down, eyes scanning, typing on
    the keyboard in front of it. No bouncing. The whole scene sits HACK_UP higher."""
    out = []
    kb_w = 54; kb_h = 20
    kb_x = (CANVAS - kb_w) // 2
    kb_y = CY + CH - 3 - HACK_UP           # at clawd's feet, shifted up
    NK = 30                                # approximate number of grid keys
    for i in range(n):
        ph = i / n * 2 * math.pi
        larm = 1 if math.sin(ph) > 0 else 0          # arms in antiphase, ±1px
        rarm = 1 if math.sin(ph) <= 0 else 0
        eye_dx = (0, 1, 0, 0, 0, -1, 0, 0)[i % 8]
        cl = clawd_variant(eye_dx=eye_dx, larm_dy=larm, rarm_dy=rarm)
        c = base_canvas()
        place(c, cl, dy=-HACK_UP)
        d = ImageDraw.Draw(c)
        pressed = {(i * 3) % NK, (i * 5 + 4) % NK}    # different keys each frame
        if i % 4 == 3: pressed.add(99)                # hit Enter now and then
        draw_keyboard(d, kb_x, kb_y, kb_w, kb_h, pressed)
        out.append(c)
    return out

# --- experimental "working" pose: clawd cooking in a pan instead of typing ---
# Same recipe as the keyboard: clawd's own pixels are untouched, the pan is a
# purely deterministic overlay in the same HACK_UP band. Three things make it
# readable: the pan mouth must be an ellipse (a rectangle reads as a pot), the
# handle must be lighter than the pan, and the yolk must be the brightest thing
# against the dark interior.
PAN_RIM   = (152, 156, 170, 255)   # rim (bright steel against the background)
PAN_BODY  = (98, 102, 116, 255)    # outer body, the thickness below the rim
PAN_IN    = (48, 50, 60, 255)      # interior (dark cast iron)
PAN_IN_HI = (72, 76, 90, 255)      # lower interior highlight, for depth
HANDLE    = (128, 86, 54, 255)     # wooden handle
HANDLE_D  = (84, 54, 34, 255)
HANDLE_HI = (168, 118, 78, 255)
EGG_W     = (247, 243, 228, 255)   # egg white
EGG_SH    = (204, 198, 178, 255)
YOLK      = (250, 186, 52, 255)
YOLK_HI   = (255, 226, 130, 255)
STEAM     = (228, 234, 244, 255)   # must stay white over clawd's warm orange body
SIZZLE    = (255, 214, 140, 255)   # sizzle spark

PAN_W, PAN_H = 38, 13              # mouth ellipse
PAN_X = 12                         # left edge, aligned with clawd's body axis
PAN_UP = 6                         # raise the pan relative to clawd, shortening the handle

# The pan scene is shorter than the keyboard one (the pan sits at chest height,
# the keyboard at foot height), so placed in the same band it leaves a gap at the
# bottom. COOK_DOWN lowers the whole scene until its bottom edge lines up with
# hacking's, filling the 51 drawn rows.
COOK_DOWN = 11

# --- handle: the pan is rotated so the handle runs 45 degrees up and to the right ---
# clawd's right arm is a horizontal bar with its tip at (52, ~14); a flat handle
# would sit 16px below the hand. Rotating a top-down pan about its own axis does
# not change the mouth ellipse — only the handle direction — so this reaches the
# hand without touching a single sprite pixel. The 1:1 slope drops exactly 1px per
# column, giving a clean diagonal instead of an irregular staircase.
#
# The handle length is not free: at 45 degrees the tip must land on the hand at
# (53,13), so L = 22 - PAN_X and pan_y0 = 10 + L. Shortening it means moving the
# pan up and right relative to clawd, which also makes the scene read as "held at
# chest height" rather than "sitting on the floor".
HANDLE_DX = 32                    # where the handle meets the pan (upper-right of the rim)
HANDLE_DY = 2
# The handle overshoots the outer end of the arm by 3px. Ending flush reads as
# "touching the arm"; a tip poking out the far side reads as a grip.
HANDLE_L  = 13

def draw_pan(c, x, y):
    """Top-down pan: body thickness + bright rim + dark interior + a 45-degree
    wooden handle. Each handle column is 4 rows (highlight, 2 body, shadow),
    giving ~2.8px thickness along the diagonal."""
    d = ImageDraw.Draw(c)
    d.ellipse([x, y + 3, x + PAN_W - 1, y + PAN_H - 1 + 3], fill=PAN_BODY)   # thickness
    d.ellipse([x, y, x + PAN_W - 1, y + PAN_H - 1], fill=PAN_RIM)            # rim
    d.ellipse([x + 2, y + 2, x + PAN_W - 3, y + PAN_H - 3], fill=PAN_IN)     # interior
    d.arc([x + 3, y + 3, x + PAN_W - 4, y + PAN_H - 3], 25, 155, fill=PAN_IN_HI)
    hx, hy = x + HANDLE_DX, y + HANDLE_DY
    for k in range(HANDLE_L):
        cx, cy = hx + k, hy - k
        # Round off the tip: a full last column looks sawn off, so drop the
        # highlight and shadow pixels and keep only the two body pixels.
        if k == HANDLE_L - 1:
            d.point((cx, cy + 1), fill=HANDLE_HI)
            d.point((cx, cy + 2), fill=HANDLE)
            continue
        d.line([(cx, cy), (cx, cy + 2)], fill=HANDLE)
        d.point((cx, cy), fill=HANDLE_HI)                   # upper-left highlight
        d.point((cx, cy + 3), fill=HANDLE_D)                # lower-right shadow
    # The ferrule is metal — in dark wood it reads as a smudge at the handle root.
    for k in range(2):
        d.line([(hx + k, hy - k - 1), (hx + k, hy - k + 3)], fill=PAN_BODY)
        d.point((hx + k, hy - k - 1), fill=PAN_RIM)

# There is no separate "hand" sprite. A fist at the end of the handle just read
# as an odd blob at this size. Since the handle tip already reaches the outer end
# of the arm and the arm moves in phase with the pan, the grip reads on its own.

def draw_egg(c, cx, cy, flat=False):
    """Fried egg: white with a shadow plus a bright yolk. flat=True squashes it
    slightly, for the airborne frames."""
    d = ImageDraw.Draw(c)
    h = 7 if not flat else 6
    d.ellipse([cx - 8, cy - h // 2 + 1, cx + 7, cy + h // 2 + 1], fill=EGG_SH)  # shadow
    d.ellipse([cx - 8, cy - h // 2, cx + 7, cy + h // 2], fill=EGG_W)
    d.ellipse([cx - 10, cy - 2, cx - 5, cy + 2], fill=EGG_W)                   # left lobe
    d.ellipse([cx - 2, cy - 3, cx + 3, cy + 1], fill=YOLK)
    d.point((cx - 1, cy - 2), fill=YOLK_HI)

def draw_steam(c, x, y, alpha, wob=0.0, H=6):
    """A rising ribbon of steam: 2px wide, curling on a sine, fading toward the
    top. A continuous ribbon plus a vertical alpha ramp is what reads as steam —
    individual pixel glyphs looked like letters at this size."""
    p = c.load()
    r, g, b, _ = STEAM
    for k in range(H):
        dx = int(round(1.4 * math.sin(k * 0.85 + wob)))
        a = alpha * (H - k) // H                      # fades toward the top
        if a <= 12: continue
        for sx in (x + dx, x + dx + 1):
            if 0 <= sx < CANVAS and 0 <= y - k < CANVAS:
                p[sx, y - k] = (r, g, b, a)

def anim_cooking(n=8):
    """clawd flipping an egg: the right arm holds the handle, the pan dips and the
    egg jumps (frames 2-4) while the eyes follow it; steam rises and the egg
    sizzles whenever it is back in the pan."""
    out = []
    # The pan mouth cuts across clawd's feet, the same way the keyboard does, so
    # it reads as being held rather than sitting on the ground.
    pan_y0 = CY + CH - 3 - HACK_UP - 2 - PAN_UP + COOK_DOWN
    pan_dy = (0, 1, -2, -1, 0, 1, 0, 0)          # dip -> toss -> catch
    egg_dy = (0, 1, -3, -9, -6, 1, 0, 0)
    # steam: (x, phase, wobble) — spread along the mouth, all left of the handle
    # band so no ribbon crosses it.
    steam = ((16, 0, 0.0), (27, 3, 1.7), (38, 5, 3.2))
    life = 5
    for i in range(n):
        pdy = pan_dy[i % 8]
        air = i % 8 in (2, 3, 4)                 # egg airborne
        cl = clawd_variant(eye_dy=-1 if air else 0,
                           larm_dy=(0, 0, -1, -1, 0, 0, 0, 0)[i % 8],
                           rarm_dy=pdy)                     # right arm exactly in phase with the pan
        c = base_canvas()
        place(c, cl, dy=-HACK_UP + COOK_DOWN)
        py = pan_y0 + pdy
        # arm and handle move by the same amount, so the grip never breaks
        draw_pan(c, PAN_X, py)
        # steam before the egg: a ribbon drawn over an airborne egg looks like a smudge
        for (sx, ph, wob) in steam:
            age = (i - ph) % n
            if age >= life: continue
            draw_steam(c, sx, py - 1 - age, 230 - int(170 * age / life), wob + age * 0.5)
        ecx = PAN_X + PAN_W // 2 + 1                 # nudged right, the white's left lobe overhangs
        draw_egg(c, ecx, py + PAN_H // 2 + egg_dy[i % 8], flat=air)
        if not air:                              # sizzles while in the pan
            p = c.load()
            for (sx, sy) in ((ecx - 10, py + 3), (ecx + 9, py + 7)) if i % 2 == 0 else \
                            ((ecx + 10, py + 3), (ecx - 9, py + 8)):
                if 0 <= sx < CANVAS and 0 <= sy < CANVAS: p[sx, sy] = SIZZLE
        out.append(c)
    return out

# --- thought bubble (shaded puffy cloud) ---
CLOUD_W = (247, 248, 246, 255)   # top white
CLOUD_SH = (196, 199, 212, 255)  # bottom shadow (volume)
CLOUD_LN = (62, 64, 80, 255)     # outline
CLOUD_HI = (255, 255, 255, 255)  # upper-left highlight
DOT = (70, 72, 88, 255)

def _shade_from_mask(mask):
    """Turn a mask into a shaded sprite: outline + bottom shadow + upper-left highlight."""
    w, h = mask.size; mp = mask.load()
    spr = Image.new("RGBA", (w, h), (0, 0, 0, 0)); p = spr.load()
    def solid(x, y): return 0 <= x < w and 0 <= y < h and mp[x, y] >= 128
    for y in range(h):
        for x in range(w):
            if not solid(x, y): continue
            if not (solid(x-1, y) and solid(x+1, y) and solid(x, y-1) and solid(x, y+1)):
                p[x, y] = CLOUD_LN                       # edge
            elif not solid(x, y+2):                      # near the bottom -> shadow
                p[x, y] = CLOUD_SH
            elif not solid(x-1, y-1) and y < h * 0.5:    # upper-left inner rim
                p[x, y] = CLOUD_HI
            else:
                p[x, y] = CLOUD_W
    return spr

def _build_rrect(w, h, r):
    """Blocky bubble with slightly rounded corners, matching clawd's angular style."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=r, fill=255)
    return _shade_from_mask(m)

def _build_ellipse(w, h):
    """Small round puff, for the trailing bubbles."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).ellipse([0, 0, w - 1, h - 1], fill=255)
    return _shade_from_mask(m)

# main bubble plus two trailing puffs
_BUBBLE = _build_rrect(22, 12, 3)
_BUB1   = _build_rrect(5, 4, 1)
_BUB2   = _build_ellipse(4, 4)

def draw_thought_bubble(c, ndots):
    # entirely above clawd, so the head stays visible
    c.alpha_composite(_BUBBLE, (38, 0))
    c.alpha_composite(_BUB1, (34, 11))
    c.alpha_composite(_BUB2, (30, 14))                   # the puff nearest the head
    d = ImageDraw.Draw(c)
    for k in range(ndots):                               # the ... dots
        dx = 43 + k * 4
        d.rectangle([dx, 5, dx + 1, 6], fill=DOT)

def anim_think(n=8):
    """clawd thinking: a calm sway, eyes looking up and wandering, and the dots in
    the thought bubble filling in one by one."""
    out = []
    for i in range(n):
        dx = round(1.5 * math.sin(i / n * 2 * math.pi))          # slight sway, ±1
        eye_dx = (0, 0, 1, 1, 0, -1, -1, 0)[i % 8]               # slow wander
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=-1)             # eyes up
        c = base_canvas()
        place(c, cl, dx=dx)
        ndots = (i // 2) % 3 + 1                                  # cycles 1..3
        draw_thought_bubble(c, ndots)
        out.append(c)
    return out

def anim_happy(n=8):
    """Delighted: clawd hops twice, arms up while airborne, legs marching in
    antiphase, > < eyes, and hearts popping up around it."""
    out = []
    # hearts: (x, base_y, start_frame) — rises while age < life
    hearts = [(3, 26, 0), (55, 30, 2), (29, 3, 4), (2, 12, 5), (55, 14, 6)]
    life = 4
    for i in range(n):
        bounce = round(4 * abs(math.sin(i / n * 2 * math.pi)))   # 0..4, two hops
        airborne = bounce >= 3
        arm = -1 if airborne else 0                               # arms up while airborne
        # marching legs: the pairs alternate
        lleg = -1 if i % 2 == 0 else 0
        rleg = 0 if i % 2 == 0 else -1
        cl = clawd_variant(eyes="normal", larm_dy=arm, rarm_dy=arm,   # square eyes
                           lleg_dy=lleg, rleg_dy=rleg)
        c = base_canvas()
        place(c, cl, dy=-bounce)
        for (hx, hy, st) in hearts:
            age = (i - st) % n
            if age < life: draw_heart(c, hx, hy - age)
        out.append(c)
    return out

# --- oops elements: a red ! and a blue sweat drop ---
EXCL = (226, 44, 48, 255); EXCL_SH = (150, 26, 30, 255)
SWEAT = (96, 176, 228, 255); SWEAT_HI = (205, 234, 255, 255)
def draw_exclaim(c, big):
    """A bold red '!' above the head. big=True enlarges it slightly."""
    d = ImageDraw.Draw(c)
    top = 2 if big else 3
    bx = 30
    d.rectangle([bx, top, bx + 2, 9], fill=EXCL)          # stem
    d.rectangle([bx, 11, bx + 2, 12], fill=EXCL)          # dot
    d.rectangle([bx + 2, top, bx + 2, 12], fill=EXCL_SH)  # right shadow
def draw_sweat(c, i):
    """A blue sweat drop running down the right cheek, looping."""
    p = c.load()
    sy = 16 + (i % 4) * 3                                  # drips downward
    sx = 47
    drop = [(1,0),(0,1),(1,1),(2,1),(0,2),(1,2),(2,2),(1,3)]   # drop shape
    for (dx, dy) in drop:
        x, y = sx + dx, sy + dy
        if 0 <= x < CANVAS and 0 <= y < CANVAS: p[x, y] = SWEAT
    if 0 <= sx+1 < CANVAS and 0 <= sy < CANVAS: p[sx+1, sy] = SWEAT_HI  # highlight

def anim_oops(n=8):
    """Oops: clawd shakes horizontally, a red ! pulses above its head, and a sweat
    drop runs down the right cheek."""
    out = []
    shake = [-2, 2, -2, 1, -1, 2, -2, 1]
    for i in range(n):
        cl = clawd_variant()                              # body still; the shake is in the placement
        c = base_canvas()
        place(c, cl, dx=shake[i % len(shake)])
        draw_exclaim(c, big=(i % 2 == 0))
        draw_sweat(c, i)
        out.append(c)
    return out

# --- sleep: just clawd, closed eyes and breathing; no zzZZ ---
def anim_sleep(n=8):
    """Sleeping clawd: the idle breathing with closed eyes. No extra symbols."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.06 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))   # same breathing
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant(eyes="sleep")
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))     # bottom-aligned
        out.append(c)
    return out

# --- ask: clawd is asking YOU something -> a glossy amber "?" above its head ---
# The stroke is thin, so a drop shadow plus an upper-left shine reads cleaner at
# this size than a surround shader.
QBODY = (252, 206, 92, 255); QHI = (255, 246, 206, 255)
QSH   = (176, 118, 40, 255); QLN = (92, 60, 24, 255)
_QM = [(2,0),(3,0),(4,0),(5,0),(6,0),          # top arc
       (1,1),(2,1),(6,1),(7,1),
       (1,2),(2,2),(6,2),(7,2),
       (6,3),(7,3),                              # right side descending
       (5,4),(6,4),
       (4,5),(5,5),
       (3,6),(4,6),                              # diagonal into the stem
       (3,7),(4,7),
       (3,8),(4,8),                              # stem bottom
       (3,10),(4,10),                            # dot
       (3,11),(4,11)]
_QMset = set(_QM)
def _build_qmark():
    w, h = 10, 13
    spr = Image.new("RGBA", (w, h), (0, 0, 0, 0)); p = spr.load()
    for (x, y) in _QM:                            # 1) drop shadow (lower-right)
        if (x+1, y+1) not in _QMset and 0 <= x+1 < w and 0 <= y+1 < h:
            p[x+1, y+1] = QLN if ((x+1, y+2) not in _QMset) else QSH
    for (x, y) in _QM: p[x, y] = QBODY            # 2) body
    for (x, y) in _QM:                            # 3) upper-left shine
        if (x, y-1) not in _QMset and (x-1, y) not in _QMset:
            p[x, y] = QHI
    return spr
_QMARK = _build_qmark()

def anim_ask(n=8):
    """clawd asking a question: it stands calmly, eyes up at the '?', which bobs
    gently. Not an interrupt — Claude is waiting on you (AskUserQuestion /
    ExitPlanMode)."""
    out = []
    for i in range(n):
        ph = i / n * 2 * math.pi
        dx = round(1.0 * math.sin(ph))                       # very slight sway
        eye_dx = (0, 0, 1, 1, 0, -1, -1, 0)[i % 8]
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=-1)         # looking up
        c = base_canvas()
        place(c, cl, dx=dx)
        bob = round(1.5 * math.sin(ph))                      # soft bob
        c.alpha_composite(_QMARK, (27, 3 - bob))             # centred above the head
        out.append(c)
    return out

# --- agents: clawd supervising subagents -> shifts up and looks down at the minis ---
# A separate, calm "supervision" pose (no keyboard). clawd moves up so a real band
# opens at the bottom, where the firmware places the mini clawds. The device draws
# only this animation's top rows, leaving that band free.
AGENTS_UP = 16                                  # px to shift clawd up (matches animPushRows in the firmware)
def anim_agents(n=8):
    """clawd supervising its subagents: shifted up, eyes down and scanning across
    the minis, breathing calmly."""
    out = []
    for i in range(n):
        sy = 1.0 - 0.05 * (0.5 - 0.5 * math.cos(i / n * 2 * math.pi))   # soft breathing
        nh = max(1, int(round(CH * sy)))
        eye_dx = (0, 1, 1, 0, -1, -1, 0, 0)[i % 8]                       # scanning the minis
        cl = clawd_variant(eye_dx=eye_dx, eye_dy=3)                       # eyes down
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, (CY - AGENTS_UP) + (CH - nh)))
        out.append(c)
    return out

# --- love: the petting gesture -> happy > < eyes and rising PixelLab hearts ---
# Deliberately unlike happy: affection rather than celebration, so no bouncing.
def anim_love(n=8):
    """Petting: clawd stands calmly with > < eyes, swaying slightly, while hearts
    rise around it."""
    out = []
    # (x, base_y, start_frame, target_height_px) — varied sizes give depth
    hearts = [(15, 30, 0, 15), (49, 33, 2, 13), (32, 8, 3, 16),
              (7, 21, 4, 11), (57, 19, 6, 12)]
    life = 5
    for i in range(n):
        ph = i / n * 2 * math.pi
        dx = round(1.0 * math.sin(ph))                       # small sway
        breath = 1 if i % 4 in (1, 2) else 0                 # very light breathing
        cl = clawd_variant(eyes="happy", larm_dy=-breath, rarm_dy=-breath)
        c = base_canvas()
        place(c, cl, dx=dx)
        for (hx, hy, st, th) in hearts:
            age = (i - st) % n
            if age < life:
                # Always opaque: lowering alpha over the dark background greys the
                # heart out, so it rises and then vanishes instead of fading.
                draw_heart_spr(c, hx, hy - age * 3, th=th, alpha=255)
        out.append(c)
    return out

# --- brain sprite from PixelLab (08_brain.py), used as the context-fullness gauge.
# Same hybrid recipe as the heart: only the element comes from PixelLab.
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
    """Scale BRAIN_SPR to th px tall, keeping the aspect ratio."""
    w, h = BRAIN_SPR.size
    tw = max(1, round(w * th / h))
    return BRAIN_SPR.resize((tw, th), Image.NEAREST)

# All one warm red-amber family so the gauge does not read as a rainbow: empty is
# pale, full is saturated.
BRAIN_LOW     = (120, 96, 92, 255)    # empty, a pale "glass" tone in the same family
BRAIN_FULL_LO = (196, 40, 44, 255)    # liquid base
BRAIN_FULL_HI = (250, 150, 64, 255)   # liquid near the surface
BRAIN_SURF    = (255, 196, 132, 255)  # bright surface line

def _brain_shade_map(spr):
    """Extract the sprite's own fold shading as a brightness ratio (normalised to
    the mean). Multiplying the fill color by it keeps the folds visible at every
    level — a flat fill turns the brain into an unrecognisable blob."""
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
    """Brain centred at (cx,cy), th px tall, filling bottom-up like a glass with
    fill in 0..1. The empty part is pale, the filled part is warm liquid with a
    bright surface line, and the fold texture survives at every level."""
    if BRAIN_SPR is None: return
    spr = _brain_at(th); sp = spr.load(); w, h = spr.size
    shade = _brain_shade_map(spr)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0)); op = out.load()
    level_y = h - round(fill * h)                    # everything below this row is filled
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
    """Draw the brain in PixelLab's own colors, with no fill effect."""
    if BRAIN_SPR is None: return
    spr = _brain_at(th)
    c.alpha_composite(spr, (cx - spr.size[0] // 2, cy - spr.size[1] // 2))

BRAIN_CX, BRAIN_CY, BRAIN_TH = 32, 8, 15    # centred just above the head

def anim_brain_full(n=16):
    """Context filling up: clawd breathes calmly while the brain above its head
    fills like a glass and empties again, with a sweat drop running down as a
    stress marker. Shown while idle when the context is near critical."""
    out = []
    for i in range(n):
        ph = i / n * 2 * math.pi
        fill = 0.5 - 0.5 * math.cos(ph)                       # 0 -> 1 -> 0 per cycle
        sy = 1.0 - 0.05 * (0.5 - 0.5 * math.cos(ph))          # soft breathing
        nh = max(1, int(round(CH * sy)))
        cl = clawd_variant()
        cl = cl.resize((CW, nh), Image.NEAREST)
        c = base_canvas()
        c.alpha_composite(cl, (CX, CY + (CH - nh)))
        draw_brain_gauge(c, BRAIN_CX, BRAIN_CY, BRAIN_TH, fill)
        draw_sweat(c, i)                                       # stress marker, as in oops
        out.append(c)
    return out

# --- compact: the brain in its natural colors (no fill) with small stars twinkling
# above it, and clawd's eyes closed — a calm "clearing the mind" pose ---
STAR_COL = (255, 236, 158, 255)
_STAR_PX = [(1, 0), (0, 1), (1, 1), (2, 1), (1, 2)]     # tiny diamond star
def draw_star(c, cx, cy, bright):
    if bright <= 0: return
    a = max(0, min(255, round(255 * bright)))
    spr = Image.new("RGBA", (3, 3), (0, 0, 0, 0)); sp = spr.load()
    for (dx, dy) in _STAR_PX: sp[dx, dy] = (*STAR_COL[:3], a)
    c.alpha_composite(spr, (cx - 1, cy - 1))

# (x, y, phase) — independent sine phases, so they twinkle in turn
STAR_DEFS = [(16, 3, 0.0), (48, 4, 0.6), (20, 15, 1.3), (44, 16, 2.1), (32, -1, 2.8)]
STAR_DEFS = [(x, max(0, y), ph) for (x, y, ph) in STAR_DEFS]

def anim_compact(n=12):
    """Compacting: the brain in natural colors, clawd breathing peacefully with
    closed eyes, and small stars twinkling above it."""
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

# --- tickle: double-tap -> clawd giggles, shaking quickly side to side with > <
# eyes. Same horizontal-shake technique as oops, but happy. ---
def anim_tickle(n=8):
    """Being tickled: a fast, wide side-to-side shake plus a small vertical jitter,
    with > < eyes. Triggered by a double-tap; transient."""
    out = []
    shake = [-3, 3, -2, 3, -3, 2, -3, 2]                     # wider and faster than oops
    for i in range(n):
        wig = -1 if i % 2 == 0 else 0                        # small vertical giggle
        cl = clawd_variant(eyes="happy", larm_dy=wig, rarm_dy=wig)
        c = base_canvas()
        place(c, cl, dx=shake[i % len(shake)], dy=wig)
        out.append(c)
    return out

# --- second resting pose: clawd listening to music on over-ear headphones ---
# clawd's own pixels are untouched; the headphones are a purely deterministic
# overlay, the same recipe as the keyboard and the thought bubble.
HP_DARK = (46, 44, 62, 255)     # headphone body
HP_MID  = (86, 84, 112, 255)    # ear pad
HP_HI   = (168, 166, 200, 255)  # upper-left highlight
HP_ACC  = (232, 120, 70, 255)   # "driver" accent at the centre of the cup
HP_EDGE = (22, 20, 32, 255)     # very dark outline, to separate from the orange body
NOTE_A  = (120, 200, 190, 255)  # note colors, alternating teal / amber
NOTE_B  = (240, 190, 96, 255)

# --- note glyphs: a single eighth (♪) and a beamed pair (♫) ---
# Solid heads noticeably wider than the stems keep them legible at this size, and
# two different glyphs read as "music" better than one repeated symbol.
_NOTE1 = {  # 7x9  ♪
    "px": [(4,0),(4,1),(4,2),(4,3),(4,4),(4,5),(4,6),                 # stem
           (5,0),(6,1),(6,2),(5,3),                                   # flag
           (1,6),(2,6),(3,6),
           (0,7),(1,7),(2,7),(3,7),
           (1,8),(2,8),(3,8)],                                        # head
    "hi": [(1,7)], "w": 7, "h": 9,
}
_NOTE2 = {  # 8x8  ♫ — heads left of the stems, beam on top
    "px": [(2,0),(3,0),(4,0),(5,0),(6,0),(7,0),                       # beam
           (2,1),(3,1),(4,1),(5,1),(6,1),(7,1),
           (2,2),(2,3),(2,4),                                         # left stem
           (7,2),(7,3),(7,4),                                         # right stem
           (0,5),(1,5),(2,5),(0,6),(1,6),(2,6),(1,7),(2,7),           # left head
           (5,5),(6,5),(7,5),(5,6),(6,6),(7,6),(6,7),(7,7)],          # right head
    "hi": [(0,5),(5,5)], "w": 8, "h": 8,
}

def draw_note(c, x, y, col, alpha=255, glyph=0):
    """Draw a note glyph from its top-left corner. glyph 0=♪, 1=♫."""
    g = (_NOTE1, _NOTE2)[glyph % 2]
    spr = Image.new("RGBA", (g["w"], g["h"]), (0, 0, 0, 0)); p = spr.load()
    r, gg, b, _ = col
    for (nx, ny) in g["px"]: p[nx, ny] = (r, gg, b, alpha)
    for (nx, ny) in g["hi"]:                                # inner highlight = volume
        p[nx, ny] = (min(255, r + 60), min(255, gg + 60), min(255, b + 60), alpha)
    c.alpha_composite(spr, (x, y))

# --- PixelLab pixel-art headphones (from 09_headphones.py), used by idle_music ---
# Same hybrid recipe as the heart/brain sprites: clawd stays deterministic, the
# accessory is a real PixelLab-textured sprite. Comes back on a flat gray
# background, keyed out by corner color and cropped like the others.
def _load_headphones_spr():
    hp = os.path.join(HERE, "out/headphones.png")
    if not os.path.exists(hp): return None
    im = Image.open(hp).convert("RGBA"); px = im.load(); w, h = im.size
    # sample the background as the modal color, not just the (0,0) corner --
    # PixelLab sometimes returns a single fully-transparent padding pixel there,
    # whose stray RGB (usually black) would wrongly key out the dark outline.
    counts = Counter(px[x, y][:3] for y in range(h) for x in range(w) if px[x, y][3] > 0)
    bg = counts.most_common(1)[0][0]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0 or all(abs((r, g, b)[k] - bg[k]) <= 45 for k in range(3)):
                px[x, y] = (0, 0, 0, 0)                      # drop the grey background
    bb = im.getchannel("A").getbbox()
    return im.crop(bb) if bb else None
HEADPHONES_SPR = _load_headphones_spr()

def draw_headphones_spr(c, dx=0, dy=0, tw=42, th=13):
    """Composite the PixelLab headphones sprite, positioned the same as the
    code-drawn version: centered above/on clawd's head, moving with dx/dy so it
    tracks the mascot's bob and lean. Falls back to the code-drawn version when
    the sprite is missing."""
    if HEADPHONES_SPR is None:
        return draw_headphones(c, dx=dx, dy=dy)
    spr = HEADPHONES_SPR.resize((tw, th), Image.NEAREST)   # fixed target box: the
                                                             # cups must reach clawd's
                                                             # body edges to clear the
                                                             # face, aspect-correctness
                                                             # is secondary here
    cx = CX + dx + CW // 2
    cy = CY + dy + 1                    # sprite center ~ eye level, band clears the head
    c.alpha_composite(spr, (cx - tw // 2, cy - th // 2))

def draw_headphones(c, dx=0, dy=0):
    """Over-ear headphones. Three things make them legible at this size: the band
    must clear the head with a visible gap (touching reads as a hat), the cups must
    be large and vertical with a distinct inner pad, and there must be a short
    vertical yoke between band and cup — that detail is what identifies the
    silhouette. Coordinates are clawd-local, so the headphones move with the
    mascot's bob and lean."""
    p = c.load()
    def put(lx, ly, col):
        x, y = CX + dx + lx, CY + dy + ly
        if 0 <= x < CANVAS and 0 <= y < CANVAS: p[x, y] = col

    # The cups must touch the body — a 1px gap reads as a floating box.
    CUP_L, CUP_R = 1, 37
    CUP_W = 6
    # Sit the cups at eye level without entering the arms' y7 band.
    CUP_Y0, CUP_H = -1, 8
    ARM_L, ARM_R = CUP_L + 2, CUP_R + 3   # yoke x, at the centre of each cup

    # --- band: a thin 2px arc clearing the top of the head ---
    # It sits over the background, so it is drawn in light tones; a dark band would
    # disappear into it. Dark tones are used only where the part is over the body.
    for lx in range(ARM_L, ARM_R + 1):
        t = (lx - (ARM_L + ARM_R) / 2) / ((ARM_R - ARM_L) / 2)
        ly = round(-4 - 6 * (1 - t * t))            # -4 at the ends, -10 in the middle
        put(lx, ly, HP_HI)                          # bright top edge
        put(lx, ly + 1, HP_MID)                     # body tone below

    # --- yokes: a short vertical link from the band end to the top of each cup ---
    for lx in (ARM_L, ARM_R):
        for ly in range(-4, CUP_Y0 + 1):
            put(lx, ly, HP_MID); put(lx + 1, ly, HP_DARK)

    # --- ear cups ---
    # Each cup straddles two backgrounds: clawd's warm orange on the inside, the
    # dark background outside. One tone cannot separate from both, so the rim is
    # light (visible against the background) and the fill is dark (visible against
    # the orange), with a single warm accent in the middle.
    for x0 in (CUP_L, CUP_R):
        for ly in range(CUP_Y0, CUP_Y0 + CUP_H):
            edge_row = ly in (CUP_Y0, CUP_Y0 + CUP_H - 1)
            for lx in range(x0, x0 + CUP_W):
                if edge_row and lx in (x0, x0 + CUP_W - 1): continue   # rounded corner
                rim = (edge_row or lx in (x0, x0 + CUP_W - 1))
                put(lx, ly, HP_MID if rim else HP_DARK)
        put(x0 + 1, CUP_Y0 + 1, HP_HI)                                # upper-left highlight
        cy = CUP_Y0 + CUP_H // 2                                       # driver accent
        put(x0 + 2, cy, HP_ACC); put(x0 + 3, cy, HP_ACC)
        put(x0 + 2, cy + 1, HP_ACC); put(x0 + 3, cy + 1, HP_ACC)

def anim_idle_music(n=8):
    """clawd listening to music: happy > < eyes, a light head bob on the beat plus
    a lean, arms keeping time in turn, and notes drifting up on both sides. A calm
    resting pose."""
    out = []
    # notes: (x, base_y, phase, glyph) — drift up and fade over their lifetime,
    # balanced left/right and clear of clawd's arm band.
    notes = [(54, 30, 0, 0), (1, 26, 3, 1), (55, 18, 5, 1)]
    life = 5
    for i in range(n):
        beat = i % 4
        dy   = -1 if beat in (1, 2) else 0                      # 2 beats per cycle
        dx   = (0, 0, 1, 1, 0, 0, -1, -1)[i % 8]                # slight lean
        larm = -1 if beat < 2 else 0                            # arms keep time in turn
        rarm = 0 if beat < 2 else -1
        cl = clawd_variant(eyes="happy", larm_dy=larm, rarm_dy=rarm)
        c = base_canvas()
        place(c, cl, dx=dx, dy=dy)
        draw_headphones_spr(c, dx=dx, dy=dy)                    # moves with the mascot
        for k, (nx, ny, ph, gl) in enumerate(notes):
            age = (i - ph) % n
            if age >= life: continue
            a = 255 - int(190 * age / life)                     # fades as it rises
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
    print(f"{name}: {N} frames {CANVAS}x{CANVAS} -> out/anim_{name}/ , out/{name}_montage.png")

if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "hacking"
    for nm in (list(ANIMS) if which == "all" else [which]):
        save(nm, ANIMS[nm]())
