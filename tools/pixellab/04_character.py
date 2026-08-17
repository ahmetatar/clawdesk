#!/usr/bin/env python3
# Create clawd as a rigged character in PixelLab's character system, which is
# what makes /characters/animations produce consistent animations. The
# character_id is cached in out/character_id.txt — recreating it wastes credits.
#
#   source secrets.sh && python3 04_character.py
import os, sys, json, lib

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "out", "clawd_base.png")
IDFILE = os.path.join(HERE, "out", "character_id.txt")

# create-character-pro with method=rotate_character preserves the shape: it
# rotates the real sprite into 8 directions instead of redrawing it, so clawd's
# blocky silhouette survives (create-character-v3 rounded it off).
DESC = ("clawd mascot, blocky angular pixel art, square boxy rectangular body with "
        "flat straight edges and sharp corners, two big square black eyes, short stubby "
        "legs, orange-red, retro low-resolution sprite, NOT round, NOT smooth")

if os.path.exists(IDFILE) and "--force" not in sys.argv:
    cid = open(IDFILE).read().strip()
    print(f"already exists: character_id={cid}  (use --force to recreate). Dumping its structure:")
else:
    if not os.path.exists(BASE):
        raise SystemExit("out/clawd_base.png is missing")
    if os.path.exists(IDFILE):                       # archive the old id
        old = open(IDFILE).read().strip()
        open(IDFILE + ".prev", "a").write(old + "\n")
        print("archived old character_id:", old)
    print("creating the clawd character (create-character-pro, method=rotate_character, side)...")
    res = lib.post("/create-character-pro", {
        "description": DESC,
        "method": "rotate_character",
        "reference_image": lib.b64img(BASE),
        "image_size": {"width": 64, "height": 64},
        "view": "side",
        "template_id": "mannequin",
        "no_background": True,
    })
    cid = res.get("character_id")
    job = res.get("background_job_id")
    print("character_id:", cid, " job:", job, " usage:", res.get("usage"))
    with open(IDFILE, "w") as f:
        f.write(cid or "")
    if job:
        done = lib.poll_job(job, every=4, timeout=420)
        print("job status:", done.get("status"))

# --- inspect the character: directions and how rotation frames come back ---
ch = lib.get(f"/characters/{cid}")
def keys(d, pre=""):
    if isinstance(d, dict):
        for k, v in d.items():
            if isinstance(v, (dict, list)):
                n = len(v)
                print(f"{pre}{k}: {type(v).__name__}({n})")
                if isinstance(v, list) and v and isinstance(v[0], dict):
                    print(f"{pre}  [0] keys:", list(v[0].keys()))
                elif isinstance(v, dict):
                    keys(v, pre + "  ")
            else:
                sv = str(v)
                print(f"{pre}{k}: {sv[:60]}")
print("\n=== GET /characters/%s structure ===" % cid)
keys(ch)
