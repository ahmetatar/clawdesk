#!/usr/bin/env python3
# clawd maskot vitrinini uretir: docs/showcase.html (tek dosya, disa bagimlilik yok).
#
# KAYNAK = src/anims/*.h + src/anims.h, yani CIHAZIN GERCEKTEN CALISTIRDIGI veri.
# PixelLab ciktilarindan (tools/pixellab/out) DEGIL: bir animasyon elle rotuslandiysa
# (bkz. 10_hacking_space_fix.py) vitrin yine cihazdakini gosterir, sanat ile sayfa
# birbirinden kaymaz. Frame sayisi / hiz / LED rengi / kirpma satiri da ayni sebeple
# okunur, elle yazilmaz.
#
#   python3 tools/build_showcase.py                  -> docs/showcase.html (tam sayfa)
#   python3 tools/build_showcase.py --fragment PATH  -> ayni sayfa, <html>/<head>/<body>
#                                                       sarmalayicisi olmadan (Artifact
#                                                       yayini bunu ister).
#
# Yeni animasyon eklerken: src/anims.h'ye kaydettikten sonra asagidaki MEANING
# sozlugune bir satir ekle (tetikleyici + ne anlattigi), sonra scripti calistir.
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

# Cihaz gercegi (include/config.h + src/main.cpp ile ayni olmali).
SCREEN_W, SCREEN_H = 320, 240
ANIM_S = 3                       # sprite olcegi
XOFF, YOFF = 64, 24              # sprite'in ekrandaki sol-ust kosesi
HUD_TOP = 177                    # bu satirdan asagisi HUD'a ait (3 satir)
MINI_S = 1                       # mini clawd 1:1 cizilir (src/mini.h)
MINI_X, MINI_Y = 41, 130         # ilk slotun ev konumu (mini.h HOME_X / ROW_Y)

# Her maskotun ne anlattigi + neyin tetikledigi. Tetikleyiciler src/main.cpp
# mapEvent() ve dokunmatik/guc kancalarindan alindi.
MEANING = {
    "idle": ("Dinlenme", "Isin yok. Her sey bittiginde donulen sade poz; tool.post (basarili) ve session.stop buraya dusurur."),
    "hacking": ("Calisiyor", "Bir tool calistiriliyor. tool.pre her zaman klavyeyle baslar."),
    "cooking": ("Is uzadi", "Klavye pozu WORK_LONG_MS'i asinca tavaya gecer ve is bitene kadar orada kalir. Surpriz degil, bilgi: bu is kaynatiyor."),
    "think": ("Dusunuyor", "think ack, prompt.submit ve wait. Uzun frame araligi (200 ms) sakin bir tempo verir."),
    "ask": ("Sana soruyor", "AskUserQuestion ve ExitPlanMode. Bunlar da tool.pre gonderir ama calisma degil BEKLEME durumudur, o yuzden klavye yerine soru pozu."),
    "agents": ("Alt-agent'lar", "agent.spawn. Maskot yukari kayip alt sirada gezinen mini'lere bakar; ekranin yalniz ust bandi cizilir, alt bant mini'lere kalir."),
    "happy": ("Sevinc", "git olayi ve session.start. Gecici poz: HOLD_MS sonra dinlenmeye doner."),
    "oops": ("Hata", "Basarisiz tool.post. Gecici poz."),
    "compact": ("Zihin temizleniyor", "PreCompact. Beyin ve yanip sonen minik yildizlar."),
    "brain_full": ("Context kritik", "Context yuzdesi esigi asinca 'sakin dinlenme' pozunun yerini bu alir; beyin bardak gibi dolup bosalir."),
    "sleep": ("Uyuklama", "Guc yoneticisi ekrani DIM'e alinca. Uyaninca dinlenmeye doner."),
    "tickle": ("Gidiklanma", "Ekrana CIFT DOKUNUS. Dokunustan onceki poz hatirlanir, bittiginde ona donulur."),
    "love": ("Oksama", "Ekrana SURTME. Yukselen kalpler; yine dokunus oncesi poza donulur."),
    "idle_music": ("Kulaklikla muzik", "Su an HICBIR olay tetiklemiyor. Kayitta hazir duruyor; en guclu aday session.stop (“tur bitti, sira sende”)."),
}

MINI_NOTE = ("Mini clawd", "Animasyon degil, tek kare. Her aktif alt-agent icin ekranin alt bandinda bir tane gezinir.")


def parse_registry():
    """src/anims.h icindeki ANIMS tablosunu oku (sira, hiz, LED, gecicilik)."""
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
    """src/anims.h animPushRows(): cihaz bu anim'in kac satirini ciziyor."""
    return 33 if name == "agents" else 51


def read_frames(sym):
    """clawd_<sym>.h icindeki RGB565 dizisini PIL goruntulerine cevir."""
    path = os.path.join(ANIM_DIR, "clawd_%s.h" % sym.replace("clawd_", ""))
    txt = open(path).read()
    w = int(re.search(r"_W (\d+)", txt).group(1))
    h = int(re.search(r"_H (\d+)", txt).group(1))
    blocks = [b for b in re.findall(r"\{([0-9A-Fa-fx,]+)\}", txt) if b.count(",") > w]
    if not blocks:                        # tek kareli sprite (mini)
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
        # mini bir "poz" degil: hiz, LED ve gecicilik onun icin tanimsiz.
        "na": True, "transient": False,
        "frames": [data_uri(imgs[0])],
    })

    # Ekran zemini sprite'in KENDI kosesinden okunur. config.h'deki BG_R/G/B degil:
    # o deger RGB565'e yuvarlanirken kayiyor (#24272C -> #202429) ve sabit yazilirsa
    # sprite ile sayfa arasinda gorunur bir dikdortgen seam kaliyor.
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
        # <meta>/<title> disari, geri kalani (style) icerik akisinda kalir
        head = re.sub(r"<meta[^>]*>\s*", "", head)
        html = head.strip() + "\n" + body.strip() + "\n"

    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    open(dest, "w").write(html)
    total = sum(len(e["frames"]) for e in entries)
    print("yazildi %s  (%d maskot, %d kare, %d KB)"
          % (os.path.relpath(dest, ROOT), len(entries), total, len(html) // 1024))


TEMPLATE = r"""<!doctype html>
<html lang="tr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>clawd — maskot vitrini</title>
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
  /* HUD icin ayrilan alt bant — sprite oraya hic cizilmez */
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
  /* Sabit cerceve: 33 satirlik agents ve 40px mini de dahil butun kartlarin
     alt kenari ve kunye satiri ayni hizada kalsin. */
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
  <h1>Maskot vitrini</h1>
  <p class="lede">
    Cihazdaki her poz bir sey anlatir, hicbiri dekor degil. Asagidaki ekran gercek
    olculerinde: 320&times;240, sprite 64&times;64'ten 3 kat buyutulup ayni koordinata
    basiliyor, alt bant HUD'a ayrilmis durumda. Bir maskot sec, kendi hizinda oynasin.
  </p>

  <div class="stage">
    <div>
      <div class="device">
        <div class="glass" id="glass">
          <img id="hero" alt="">
          <div class="hud-reserve"><span>HUD &mdash; 3 satir</span></div>
        </div>
      </div>
      <div class="transport">
        <button id="play">Duraklat</button>
        <input type="range" id="scrub" min="0" max="7" step="1" value="0" aria-label="Kare secici">
        <span class="readout" id="readout">kare 1 / 8</span>
      </div>
    </div>

    <div class="spec">
      <h2 id="sp-label">&nbsp;</h2>
      <p class="slug" id="sp-slug">&nbsp;</p>
      <p class="note" id="sp-note">&nbsp;</p>
      <dl class="facts">
        <div class="fact"><dt>Kare</dt><dd id="sp-frames">&mdash;</dd></div>
        <div class="fact"><dt>Kare basi</dt><dd id="sp-interval">&mdash;</dd></div>
        <div class="fact"><dt>Tur suresi</dt><dd id="sp-loop">&mdash;</dd></div>
        <div class="fact"><dt>Cizilen satir</dt><dd id="sp-rows">&mdash;</dd></div>
        <div class="fact"><dt>RGB LED</dt><dd><span class="led" id="sp-led" data-led="off">&mdash;</span></dd></div>
        <div class="fact"><dt>Poz</dt><dd id="sp-hold">&mdash;</dd></div>
      </dl>
    </div>
  </div>

  <h3>Butun maskotlar</h3>
  <p>Hepsi kendi hizinda oynuyor. Birine tikla, yukaridaki ekrana gelsin.</p>
  <div class="grid" id="grid"></div>

  <footer>
    Bu sayfa <code>python3 tools/build_showcase.py</code> ile uretilir. Kaynak
    <code>src/anims/*.h</code> ve <code>src/anims.h</code>, yani cihazin gercekten
    calistirdigi veri &mdash; hiz, LED rengi ve kirpma satiri dahil her sey oradan
    okunur. Sanat degisince sayfayi yeniden uret.
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
    ? "kare " + (frame + 1) + " / " + cur.frames.length
    : "tek kare";
}

function start() {
  stop();
  if (cur.frames.length < 2 || !cur.interval) { play.textContent = "Oynat"; return; }
  timer = setInterval(() => { frame = (frame + 1) % cur.frames.length; paint(); }, cur.interval);
  play.textContent = "Duraklat";
}
function stop() { clearInterval(timer); timer = null; play.textContent = "Oynat"; }

function select(a) {
  cur = a; frame = 0;
  hero.alt = a.label + " pozu";
  hero.style.setProperty("--x", a.x);
  hero.style.setProperty("--y", a.y);
  hero.style.setProperty("--sw", a.w * a.scale);
  scrub.max = Math.max(0, a.frames.length - 1);
  scrub.disabled = a.frames.length < 2;

  $("sp-label").textContent = a.label;
  $("sp-slug").textContent = "ANIM_" + a.name.toUpperCase();
  $("sp-note").textContent = a.note;
  $("sp-frames").textContent = a.frames.length + (a.frames.length > 1 ? " kare" : " kare (statik)");
  $("sp-interval").textContent = a.interval ? a.interval + " ms" : "—";
  $("sp-loop").textContent = a.interval
    ? (a.interval * a.frames.length / 1000).toFixed(2).replace(".", ",") + " sn"
    : "—";
  $("sp-rows").textContent = a.rows + " / " + a.full;
  const led = $("sp-led");
  led.dataset.led = a.na ? "na" : a.led;
  led.textContent = a.na ? "—"
    : { off: "kapali", green: "yesil", blue: "mavi", cyan: "camgobegi" }[a.led];
  $("sp-hold").textContent = a.na ? "—" : (a.transient ? "gecici" : "kalici");

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
