# PixelLab → clawd animation pipeline

Tools for generating clawd's pixel art with the PixelLab API and pushing it to
the CYD display as RGB565 frames. **Recipe: art = PixelLab, motion =
deterministic code (hybrid).**

## Why hybrid?

- `animate-with-text-v2/v3` redraws every frame → the character drifts and props
  jitter.
- `create-character-v3` **re-imagines** the reference → clawd gets rounder and
  loses its identity.
- **`create-character-pro` + `method=rotate_character`** rotates the reference
  without redrawing it → clawd's **blocky silhouette survives**. This is what we use.
- Animating from the character with `mode=v3` is consistent but still warps the
  head and arms slightly.
- So: **one clean frame from PixelLab** + **deterministic motion in code**
  (`clawd_anim.py`) → zero warping, clawd the same size in every scene. Moods are
  distinguished by a **distinctive element** (hacking → keyboard, happy → sparks).

## Setup

`source secrets.sh` (API key, gitignored). `pip install Pillow`. Behind a TLS
proxy, build/flash needs `SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem`.

## Flow

```bash
cd tools/pixellab && source secrets.sh

# 0) credits / plan
python3 00_balance.py

# 1) clawd base sprite (image-to-pixelart)             -> out/clawd_base.png
python3 01_clawd_base.py A 64

# 2) turn clawd into a faithful rigged character (rotate_character, stays blocky)
#    -> character_id in out/character_id.txt, 8-direction rotation in char_zip2
python3 04_character.py            # --force to recreate
#    canonical front frame: out/clawd_south.png (used by clawd_anim.py)

# 3a) DETERMINISTIC animation (idle, hacking, ...) — recommended, clean, 0 credits
python3 clawd_anim.py all          # -> out/anim_<name>/ + out/<name>_montage.png

# 3b) (optional) v3/template animation from the character — costs credits, may warp
python3 05_char_anim.py <name> v3 "<action>" south 8

# 4) convert frames to an RGB565 header                -> out/<name>.h
python3 03_png_to_header.py out/anim_idle    clawd_idle
python3 03_png_to_header.py out/anim_hacking clawd_hacking
```

## clawd_anim.py (the main animation tool)

- Fixed **64×64 canvas**; clawd is the **same size and position** in every scene.
- Canonical clawd = `out/clawd_south.png` (the rotated character's front view).
- Motion without redrawing pixels: eye scanning, arm movement (the side nubs at
  y7–14), a vertical breathing squash. Mood-specific elements are drawn in code
  (e.g. `draw_keyboard`).
- To add a mood: write `anim_<name>()` and add it to `ANIMS`.

## Notes

- 64×64 RGB565 = 8KB per frame. The device scales by `S` (S=3 for 192px).
- Headers are copied into `src/anims/`; the device pushes frames with `pushImage`.
- `examples/09-clawd-anim` is the older v2-idle approach (archived, still works).
- Schemas: `https://api.pixellab.ai/v2/openapi.json`, LLM doc: `/v2/llms.txt`.
