# PixelLab API helpers (no dependencies: urllib + base64).
import os, json, base64, urllib.request, urllib.error, time

BASE = os.environ.get("PIXELLAB_BASE_URL", "https://api.pixellab.ai/v2").rstrip("/")
KEY  = os.environ.get("PIXELLAB_API_KEY", "").strip()

def _req(method, path, body=None, retries=4):
    if not KEY:
        raise SystemExit("PIXELLAB_API_KEY is empty. Run: source tools/pixellab/secrets.sh")
    url = BASE + path
    data = json.dumps(body).encode() if body is not None else None
    last = None
    for attempt in range(retries):
        r = urllib.request.Request(url, data=data, method=method, headers={
            "Authorization": "Bearer " + KEY,
            "Content-Type": "application/json",
        })
        try:
            with urllib.request.urlopen(r, timeout=180) as resp:
                return json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            if e.code in (429, 500, 502, 503, 504):      # transient -> retry
                last = f"HTTP {e.code}"; time.sleep(3 * (attempt + 1)); continue
            raise SystemExit(f"HTTP {e.code} {path}: {e.read().decode()[:500]}")
        except (urllib.error.URLError, TimeoutError) as e:
            last = str(e); time.sleep(3 * (attempt + 1)); continue
    raise SystemExit(f"{path} failed after retries: {last}")

def post(path, body): return _req("POST", path, body)
def get(path):        return _req("GET", path)

def png_b64(path):
    # PixelLab wants raw base64, without the data-uri prefix
    with open(path, "rb") as f:
        return base64.b64encode(f.read()).decode()

def b64img(path):
    return {"type": "base64", "base64": png_b64(path)}

def save_image_field(field, out_path):
    """{type:base64, base64:'data:...'} -> PNG file"""
    b64 = field["base64"].split(",", 1)[-1]
    with open(out_path, "wb") as f:
        f.write(base64.b64decode(b64))
    print("saved:", out_path)

def poll_job(job_id, every=3, timeout=300):
    t0 = time.time()
    while time.time() - t0 < timeout:
        j = get(f"/background-jobs/{job_id}")
        st = j.get("status")
        print("  job", job_id[:8], st)
        if st in ("completed", "failed", "error"):
            return j
        time.sleep(every)
    raise SystemExit("job timeout: " + job_id)
