#!/usr/bin/env python3
# Generate one animation from clawd_base.png (animate-with-text-v2). The job runs
# in the background; frames are polled and saved to out/anim_<name>/frame_*.png.
#
#   source tools/pixellab/secrets.sh && python3 tools/pixellab/02_animate.py <name> "<action>" [SIZE]
# example: python3 tools/pixellab/02_animate.py idle "idle breathing, gentle bob" 64
import sys, os, lib
from PIL import Image

name   = sys.argv[1] if len(sys.argv) > 1 else "idle"
action = sys.argv[2] if len(sys.argv) > 2 else "idle breathing, gentle bob"
size   = int(sys.argv[3]) if len(sys.argv) > 3 else 64

base = "out/clawd_base.png"
if not os.path.exists(base):
    raise SystemExit("run 01_clawd_base.py first (out/clawd_base.png is missing)")
w, h = Image.open(base).size

print(f"animate '{name}': {action}  ({w}x{h})")
res = lib.post("/animate-with-text-v2", {
    "reference_image": lib.b64img(base),                 # {type:base64, base64:raw}
    "reference_image_size": {"width": w, "height": h},
    "action": action,
    "image_size": {"width": size, "height": size},
    "view": "side",
})

job = res.get("background_job_id")
done = lib.poll_job(job) if job else res
# frames arrive under last_response.images
imgs = (done.get("last_response") or {}).get("images") or done.get("images") or []

outdir = f"out/anim_{name}"
os.makedirs(outdir, exist_ok=True)
for i, im in enumerate(imgs):
    lib.save_image_field(im, f"{outdir}/frame_{i:02d}.png")
print(f"{len(imgs)} frame -> {outdir}")
u = (done.get("usage") or {})
print("usage:", u.get("generations"), "generations")
