#!/usr/bin/env python3
# Builds the clawd mascot showcase: docs/showcase.html, one self-contained file.
#
# The source is src/anims/*.h + src/anims.h — the data the device actually runs,
# not the PixelLab output. If an animation was retouched by hand (see
# 10_hacking_space_fix.py) the showcase still shows what ships. Frame count,
# speed, LED color and clip rows are read from the same place rather than
# hand-written.
#
#   python3 tools/build_showcase.py                  -> docs/showcase.html
#   python3 tools/build_showcase.py --fragment PATH  -> the same page without the
#                                                       <html>/<head>/<body> wrapper
#
# When adding an animation: register it in src/anims.h, add a line to MEANING
# below (trigger + what it says), then run this script.
import base64
import io
import json
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANIMS_H = os.path.join(ROOT, "src", "anims.h")
ANIM_DIR = os.path.join(ROOT, "src", "anims")
OUT = os.path.join(ROOT, "docs", "showcase.html")

# Device facts — must match include/config.h and src/main.cpp.
SCREEN_W, SCREEN_H = 320, 240
ANIM_S = 3                       # sprite scale
XOFF, YOFF = 64, 24              # sprite's top-left corner on screen
HUD_TOP = 177                    # everything below this row belongs to the HUD
MINI_S = 1                       # mini clawd is drawn 1:1 (src/mini.h)
MINI_X, MINI_Y = 41, 130         # home position of the first slot (mini.h)

# What each pose says and what triggers it, taken from mapEvent() and the touch /
# power hooks in src/main.cpp.
MEANING = {
    "idle": ("Resting", "Nothing to do. The plain pose everything returns to; a successful tool.post and session.stop both land here."),
    "hacking": ("Working", "A tool is running. tool.pre always starts at the keyboard."),
    "cooking": ("Long job", "Kept as source but not built into the firmware; the working pose is always the keyboard."),
    "think": ("Thinking", "think ack, prompt.submit and wait. The long frame interval (200 ms) sets a calm pace."),
    "ask": ("Asking you", "AskUserQuestion and ExitPlanMode. These also arrive as tool.pre, but they mean waiting rather than working — hence the question pose."),
    "agents": ("Subagents", "agent.spawn. The mascot shifts up and looks down at the row of minis; only the top band is drawn, leaving the bottom to them."),
    "happy": ("Delight", "git events and session.start. Transient: returns to rest after HOLD_MS."),
    "oops": ("Error", "A failed tool.post. Transient."),
    "compact": ("Clearing the mind", "PreCompact. The brain plus twinkling stars."),
    "brain_full": ("Context critical", "Once the context percentage crosses the threshold, this replaces the resting pose; the brain fills and empties like a glass."),
    "sleep": ("Dozing", "Shown once the power manager dims the screen. Returns to rest on wake."),
    "tickle": ("Tickled", "A double-tap on the screen. The pre-touch pose is remembered and restored afterwards."),
    "love": ("Petted", "A stroke across the screen. Rising hearts, then back to the pre-touch pose."),
    "idle_music": ("Music on headphones", "Nothing triggers it yet. Registered and ready; the likeliest owner is session.stop."),
}

MINI_NOTE = ("Mini clawd", "A single frame, not an animation. One sways in the bottom band for each active subagent.")


def parse_registry():
    """Read the ANIMS table from src/anims.h (order, speed, LED, transience)."""
    src = open(ANIMS_H).read()
    table = src.split("static const Anim ANIMS[ANIM_COUNT] = {", 1)[1].split("\n};", 1)[0]
    row = re.compile(
        r'\{\s*(\w+)\s*,\s*\w+\s*,\s*(\d+)\s*,\s*(true|false)\s*,\s*(true|false)\s*,\s*(true|false)\s*,\s*"(\w+)"'
    )
    out = []
    for sym, interval, transient, led_g, led_b, name in row.findall(table):
        out.append({
            "sym": sym,
            "name": name,
            "interval": int(interval),
            "transient": transient == "true",
            "led": {"gb": "cyan", "g": "green", "b": "blue", "": "off"}[
                ("g" if led_g == "true" else "") + ("b" if led_b == "true" else "")
            ],
        })
    return out


def push_rows(name):
    """Mirrors animPushRows() in src/anims.h: how many rows the device draws."""
    return 33 if name == "agents" else 51


def read_frames(sym):
    """Convert the RGB565 array in clawd_<sym>.h into PIL images."""
    path = os.path.join(ANIM_DIR, "clawd_%s.h" % sym.replace("clawd_", ""))
    txt = open(path).read()
    w = int(re.search(r"_W (\d+)", txt).group(1))
    h = int(re.search(r"_H (\d+)", txt).group(1))
    blocks = [b for b in re.findall(r"\{([0-9A-Fa-fx,]+)\}", txt) if b.count(",") > w]
    if not blocks:                        # single-frame sprite (mini)
        blocks = re.findall(r"\{([0-9A-Fa-fx,]+)\}", txt)
    imgs = []
    for b in blocks:
        vals = [int(v, 16) for v in b.split(",") if v]
        im = Image.new("RGB", (w, h))
        im.putdata([
            (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
            for c in vals
        ])
        imgs.append(im)
    return imgs, w, h


def data_uri(im):
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def build():
    entries = []
    for a in parse_registry():
        imgs, w, h = read_frames(a["sym"])
        rows = push_rows(a["name"])
        label, note = MEANING.get(a["name"], (a["name"], ""))
        entries.append({
            "name": a["name"],
            "label": label,
            "note": note,
            "interval": a["interval"],
            "transient": a["transient"],
            "led": a["led"],
            "rows": rows,
            "full": h,
            "w": w,
            "scale": ANIM_S,
            "x": XOFF,
            "y": YOFF,
            "frames": [data_uri(im.crop((0, 0, w, rows))) for im in imgs],
        })

    imgs, w, h = read_frames("mini")
    entries.append({
        "name": "mini", "label": MINI_NOTE[0], "note": MINI_NOTE[1],
        "interval": 0, "led": "off", "rows": h, "full": h, "w": w,
        "scale": MINI_S, "x": MINI_X, "y": MINI_Y,
        # the mini is not a pose: speed, LED and transience do not apply
        "na": True, "transient": False,
        "frames": [data_uri(imgs[0])],
    })

    # Read the screen background from the sprite's own corner, not from config.h:
    # BG_R/G/B shifts when rounded to RGB565, and hard-coding it leaves a visible
    # seam between the sprite and the page.
    bg = "#%02X%02X%02X" % read_frames("idle")[0][0].getpixel((0, 0))

    html = TEMPLATE.replace("__DEVICE_BG__", bg).replace("__DATA__", json.dumps({
        "anims": entries,
        "screen": {"w": SCREEN_W, "h": SCREEN_H, "s": ANIM_S,
                   "x": XOFF, "y": YOFF, "hud": HUD_TOP},
    }))

    dest = OUT
    if "--fragment" in sys.argv:
        dest = sys.argv[sys.argv.index("--fragment") + 1]
        head = html.split("<head>", 1)[1].split("</head>", 1)[0]
        body = html.split("<body>", 1)[1].split("</body>", 1)[0]
        # drop <meta>/<title>; the rest (style) stays in the content flow
        head = re.sub(r"<meta[^>]*>\s*", "", head)
        html = head.strip() + "\n" + body.strip() + "\n"

    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    open(dest, "w").write(html)
    total = sum(len(e["frames"]) for e in entries)
    print("wrote %s  (%d poses, %d frames, %d KB)"
          % (os.path.relpath(dest, ROOT), len(entries), total, len(html) // 1024))


TEMPLATE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>clawd — Mascot Showcase</title>
<style>
  :root {
    color-scheme: light dark;
    --ground: #EDEBF1; --panel: #FBFAFD; --edge: #D7D3E1;
    --ink: #1A1922; --dim: #5F5C72;
    --accent: #C2451F; --signal: #2F7F76;
    --device: __DEVICE_BG__; --bezel: #B4AFC2;
    --led-green: #3F9E5C; --led-blue: #3C6FD0; --led-cyan: #1F8A93;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --ground: #17181D; --panel: #1F2127; --edge: #31303C;
      --ink: #E9E7EF; --dim: #9793A9;
      --accent: #E5643F; --signal: #60B4AA;
      --device: __DEVICE_BG__; --bezel: #0E0F13;
      --led-green: #5BD07F; --led-blue: #6E9BF2; --led-cyan: #4FC7CE;
    }
  }
  :root[data-theme="light"] {
    --ground: #EDEBF1; --panel: #FBFAFD; --edge: #D7D3E1;
    --ink: #1A1922; --dim: #5F5C72;
    --accent: #C2451F; --signal: #2F7F76;
    --device: __DEVICE_BG__; --bezel: #B4AFC2;
    --led-green: #3F9E5C; --led-blue: #3C6FD0; --led-cyan: #1F8A93;
  }
  :root[data-theme="dark"] {
    --ground: #17181D; --panel: #1F2127; --edge: #31303C;
    --ink: #E9E7EF; --dim: #9793A9;
    --accent: #E5643F; --signal: #60B4AA;
    --device: __DEVICE_BG__; --bezel: #0E0F13;
    --led-green: #5BD07F; --led-blue: #6E9BF2; --led-cyan: #4FC7CE;
  }

  * { box-sizing: border-box; }
  body {
    margin: 0; background: var(--ground); color: var(--ink);
    font: 15px/1.65 -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
    -webkit-font-smoothing: antialiased;
  }
  .wrap { max-width: 1080px; margin: 0 auto; padding: 52px 24px 90px; }

  .mono { font-family: ui-monospace, "SF Mono", SFMono-Regular, Menlo, Consolas, monospace; }
  .eyebrow {
    font: 600 11px/1 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.2em; text-transform: uppercase; color: var(--accent); margin: 0 0 14px;
  }
  h1 {
    font: 600 clamp(28px, 5vw, 42px)/1.1 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: -0.03em; text-wrap: balance; margin: 0 0 14px;
  }
  .lede { max-width: 64ch; color: var(--dim); margin: 0; }

  /* ---- sahne: gercek olculerde CYD ekrani ---- */
  .stage { display: grid; grid-template-columns: minmax(0, 1fr) 260px; gap: 32px; margin: 44px 0 0; align-items: start; }
  @media (max-width: 820px) { .stage { grid-template-columns: 1fr; } }

  .device {
    background: var(--bezel); padding: 14px; border-radius: 10px;
    display: flex; justify-content: center;
  }
  .glass {
    position: relative; background: var(--device);
    width: 100%; max-width: 640px; aspect-ratio: 320 / 240;
    container-type: inline-size; overflow: hidden;
  }
  .glass img {
    position: absolute; image-rendering: pixelated; display: block;
    left: calc(var(--x) / 320 * 100%);
    top: calc(var(--y) / 240 * 100%);
    width: calc(var(--sw) / 320 * 100%);
  }
  /* the bottom band reserved for the HUD — the sprite is never drawn there */
  .hud-reserve {
    position: absolute; left: 0; right: 0; bottom: 0;
    height: calc((240 - 177) / 240 * 100%);
    border-top: 1px dashed color-mix(in srgb, var(--signal) 45%, transparent);
    display: flex; align-items: center; padding-left: 3.5%;
  }
  .hud-reserve span {
    font: 600 10px/1 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.16em; text-transform: uppercase;
    color: color-mix(in srgb, var(--signal) 75%, transparent);
  }
  @container (max-width: 380px) { .hud-reserve span { display: none; } }

  /* ---- kunye ---- */
  .spec h2 {
    font: 600 clamp(20px, 2.6vw, 26px)/1.2 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: -0.02em; margin: 0 0 4px;
  }
  .spec .slug {
    font: 12px/1 ui-monospace, "SF Mono", Menlo, monospace;
    color: var(--dim); letter-spacing: 0.04em; margin: 0 0 14px;
  }
  .spec p.note { color: var(--dim); margin: 0 0 22px; font-size: 14px; }
  .facts { display: grid; gap: 1px; background: var(--edge); border: 1px solid var(--edge); border-radius: 7px; overflow: hidden; }
  .fact { background: var(--panel); display: flex; justify-content: space-between; gap: 12px; padding: 9px 12px; }
  .fact dt {
    font: 600 10px/1.6 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.14em; text-transform: uppercase; color: var(--dim); margin: 0;
  }
  .fact dd {
    margin: 0; font: 13px/1.6 ui-monospace, "SF Mono", Menlo, monospace;
    font-variant-numeric: tabular-nums; text-align: right;
  }

  .led { display: inline-flex; align-items: center; gap: 6px; }
  .led::before { content: ""; width: 8px; height: 8px; border-radius: 50%; background: currentColor; }
  .led[data-led="na"] { color: var(--dim); }
  .led[data-led="na"]::before { display: none; }
  .led[data-led="off"] { color: var(--dim); }
  .led[data-led="off"]::before { background: none; border: 1px solid currentColor; }
  .led[data-led="green"] { color: var(--led-green); }
  .led[data-led="blue"] { color: var(--led-blue); }
  .led[data-led="cyan"] { color: var(--led-cyan); }

  /* ---- transport ---- */
  .transport {
    display: flex; align-items: center; gap: 16px; flex-wrap: wrap;
    margin-top: 14px; padding: 13px 16px;
    background: var(--panel); border: 1px solid var(--edge); border-radius: 8px;
  }
  button {
    font: 600 11px/1 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.12em; text-transform: uppercase;
    color: var(--ink); background: transparent;
    border: 1px solid var(--edge); border-radius: 5px;
    padding: 9px 15px; cursor: pointer;
  }
  button:hover { border-color: var(--accent); color: var(--accent); }
  button:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
  #play { min-width: 92px; }
  input[type=range] { flex: 1 1 160px; min-width: 120px; accent-color: var(--accent); }
  input[type=range]:focus-visible { outline: 2px solid var(--accent); outline-offset: 4px; }
  .readout {
    font: 12px/1 ui-monospace, "SF Mono", Menlo, monospace;
    font-variant-numeric: tabular-nums; color: var(--dim);
  }

  /* ---- galeri ---- */
  h3 {
    font: 600 11px/1 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.2em; text-transform: uppercase; color: var(--dim);
    margin: 62px 0 4px;
  }
  h3 + p { color: var(--dim); font-size: 14px; margin: 0 0 20px; max-width: 60ch; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px; }
  .card {
    padding: 0; background: var(--panel); border: 1px solid var(--edge); border-radius: 8px;
    overflow: hidden; cursor: pointer; text-align: left; display: flex; flex-direction: column;
    text-transform: none; letter-spacing: normal; font-family: inherit; color: inherit;
  }
  .card:hover { border-color: color-mix(in srgb, var(--accent) 55%, var(--edge)); }
  .card[aria-pressed="true"] { border-color: var(--accent); box-shadow: inset 0 0 0 1px var(--accent); }
  /* Fixed frame so every card's bottom edge and caption line up, including the
     33-row agents pose and the 40px mini. */
  .card .film { background: var(--device); position: relative; aspect-ratio: 64 / 51; }
  .card img {
    image-rendering: pixelated; display: block;
    position: absolute; bottom: 0; left: 50%; transform: translateX(-50%);
    width: calc(var(--w) / 64 * 100%);
  }
  .card .meta { padding: 9px 11px 11px; display: flex; flex-direction: column; gap: 3px; }
  .card b {
    font: 600 13px/1.3 ui-monospace, "SF Mono", Menlo, monospace; letter-spacing: -0.01em;
  }
  .card small {
    font: 10px/1.4 ui-monospace, "SF Mono", Menlo, monospace;
    letter-spacing: 0.1em; text-transform: uppercase; color: var(--dim);
    display: flex; align-items: center; gap: 8px;
  }
  .card .led::before { width: 6px; height: 6px; }

  footer {
    margin-top: 64px; padding-top: 20px; border-top: 1px solid var(--edge);
    color: var(--dim); font-size: 13px; max-width: 70ch;
  }
  footer code {
    font: 12px/1 ui-monospace, "SF Mono", Menlo, monospace;
    background: var(--panel); border: 1px solid var(--edge); padding: 2px 5px; border-radius: 3px;
  }
  @media (prefers-reduced-motion: reduce) { .card img { animation: none; } }
</style>
</head>
<body>
<div class="wrap">
  <p class="eyebrow">clawd · ESP32-2432S028R</p>
  <h1>Mascot showcase</h1>
  <p class="lede">
    Every pose on the device means something; none of them is decoration. The
    screen below is shown at its real size: 320&times;240, with the 64&times;64
    sprite scaled 3&times; and pushed to the same coordinates, and the bottom band
    reserved for the HUD. Pick a pose and it plays at its own speed.
  </p>

  <div class="stage">
    <div>
      <div class="device">
        <div class="glass" id="glass">
          <img id="hero" alt="">
          <div class="hud-reserve"><span>HUD &mdash; 3 lines</span></div>
        </div>
      </div>
      <div class="transport">
        <button id="play">Pause</button>
        <input type="range" id="scrub" min="0" max="7" step="1" value="0" aria-label="Frame selector">
        <span class="readout" id="readout">frame 1 / 8</span>
      </div>
    </div>

    <div class="spec">
      <h2 id="sp-label">&nbsp;</h2>
      <p class="slug" id="sp-slug">&nbsp;</p>
      <p class="note" id="sp-note">&nbsp;</p>
      <dl class="facts">
        <div class="fact"><dt>Frames</dt><dd id="sp-frames">&mdash;</dd></div>
        <div class="fact"><dt>Per frame</dt><dd id="sp-interval">&mdash;</dd></div>
        <div class="fact"><dt>Loop</dt><dd id="sp-loop">&mdash;</dd></div>
        <div class="fact"><dt>Rows drawn</dt><dd id="sp-rows">&mdash;</dd></div>
        <div class="fact"><dt>RGB LED</dt><dd><span class="led" id="sp-led" data-led="off">&mdash;</span></dd></div>
        <div class="fact"><dt>Pose</dt><dd id="sp-hold">&mdash;</dd></div>
      </dl>
    </div>
  </div>

  <h3>All poses</h3>
  <p>Each plays at its own speed. Click one to load it into the screen above.</p>
  <div class="grid" id="grid"></div>

  <footer>
    This page is generated by <code>python3 tools/build_showcase.py</code>. The
    source is <code>src/anims/*.h</code> and <code>src/anims.h</code> &mdash; the
    data the device actually runs, including speed, LED color and clip rows.
    Regenerate the page whenever the art changes.
  </footer>
</div>

<script>
const DATA = __DATA__;
const A = DATA.anims, S = DATA.screen;
const $ = id => document.getElementById(id);
const hero = $("hero"), scrub = $("scrub"), readout = $("readout"), play = $("play");
const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

let cur = A[0], frame = 0, timer = null;

A.forEach(a => a.frames.forEach(src => { const i = new Image(); i.src = src; }));

function paint() {
  hero.src = cur.frames[frame];
  scrub.value = frame;
  readout.textContent = cur.frames.length > 1
    ? "frame " + (frame + 1) + " / " + cur.frames.length
    : "single frame";
}

function start() {
  stop();
  if (cur.frames.length < 2 || !cur.interval) { play.textContent = "Play"; return; }
  timer = setInterval(() => { frame = (frame + 1) % cur.frames.length; paint(); }, cur.interval);
  play.textContent = "Pause";
}
function stop() { clearInterval(timer); timer = null; play.textContent = "Play"; }

function select(a) {
  cur = a; frame = 0;
  hero.alt = a.label + " pose";
  hero.style.setProperty("--x", a.x);
  hero.style.setProperty("--y", a.y);
  hero.style.setProperty("--sw", a.w * a.scale);
  scrub.max = Math.max(0, a.frames.length - 1);
  scrub.disabled = a.frames.length < 2;

  $("sp-label").textContent = a.label;
  $("sp-slug").textContent = "ANIM_" + a.name.toUpperCase();
  $("sp-note").textContent = a.note;
  $("sp-frames").textContent = a.frames.length + (a.frames.length > 1 ? " frames" : " frame (static)");
  $("sp-interval").textContent = a.interval ? a.interval + " ms" : "—";
  $("sp-loop").textContent = a.interval
    ? (a.interval * a.frames.length / 1000).toFixed(2) + " s"
    : "—";
  $("sp-rows").textContent = a.rows + " / " + a.full;
  const led = $("sp-led");
  led.dataset.led = a.na ? "na" : a.led;
  led.textContent = a.na ? "—"
    : { off: "off", green: "green", blue: "blue", cyan: "cyan" }[a.led];
  $("sp-hold").textContent = a.na ? "—" : (a.transient ? "transient" : "persistent");

  document.querySelectorAll(".card").forEach(c =>
    c.setAttribute("aria-pressed", String(c.dataset.name === a.name)));

  paint();
  if (!reduce) start(); else stop();
}

play.addEventListener("click", () => (timer ? stop() : start()));
scrub.addEventListener("input", () => { stop(); frame = +scrub.value; paint(); });

const grid = $("grid");
A.forEach(a => {
  const card = document.createElement("button");
  card.className = "card";
  card.dataset.name = a.name;
  card.setAttribute("aria-pressed", "false");
  card.innerHTML =
    '<span class="film"><img alt=""></span>' +
    '<span class="meta"><b></b><small><span class="led" data-led="' + (a.na ? "na" : a.led) + '"></span>' +
    '<span></span></small></span>';
  card.querySelector("b").textContent = a.label;
  card.querySelector("small span:last-child").textContent = a.name;
  const img = card.querySelector("img");
  img.style.setProperty("--w", a.w);
  img.src = a.frames[0];
  if (a.frames.length > 1 && a.interval && !reduce) {
    let i = 0;
    setInterval(() => { i = (i + 1) % a.frames.length; img.src = a.frames[i]; }, a.interval);
  }
  card.addEventListener("click", () => select(a));
  grid.append(card);
});

select(A[0]);
</script>
</body>
</html>
"""

if __name__ == "__main__":
    build()
