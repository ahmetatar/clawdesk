// clawd — main application (CYD / ESP32-2432S028R), landscape.
//
// A physical Claude Code mascot: it receives POST /e events over WiFi and reacts
// with expression animations.
//
// Power management (see power.h):
//   30 s with no events  -> backlight dims to ~11%, animation keeps running
//   120 s with no events -> screen off, animation stopped, CPU 80MHz, modem-sleep
//   POST /e or a touch   -> instant full brightness + 240MHz + animation
//
// Architecture: AsyncWebServer callbacks run on a separate task and must never
// touch SPI. The /e callback pushes onto a thread-safe queue; all drawing and
// touch handling happens in loop().

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "secrets.h"
#include "anims.h"
#include "power.h"
#include "hud.h"
#include "mini.h"
#include "usage.h"
#include "spinner_words.h"

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(T_CS);
AsyncWebServer server(80);
PowerManager power;
Hud hud;
MiniFleet minis;   // subagent visualization: a row of small clawds
UsageScreen usage; // quota view, reached via the top-right corner button

// ---- view mode ----
// false = mascot + HUD, true = usage cards. A short touch STARTING in the
// top-right corner switches. Event handling and power management run unchanged
// in either mode, so returning resumes where it left off.
static bool uiUsage = false;

// ---- event queue ----
struct Ev { char k[24]; char g[10]; char s[48]; char tool[16]; bool ok; bool on; int ctx; };
QueueHandle_t evq;

// ---- status queue: Claude Code statusLine summary (POST /status) ----
// Separate queue because status must NOT affect power management — it neither
// resets the idle timer nor wakes the device. Length 1 + overwrite: latest wins.
struct StatusMsg { char m[12]; int ctx; int h5; int wk; char h5r[10]; char wkr[10]; };
QueueHandle_t statusq;

// ---- spinner pool override (POST /words) ----
// Lets clawd:sync-spinner-words push Claude Code's real spinner list to the
// device. The async callback parses it into a heap SpinPool and queues the
// pointer; the swap and the free() of the old pool happen in loop(), so only the
// loop task ever touches g_spin — lock-free and thread-safe.
struct SpinPool { char *blob; const char **idx; int n; };
QueueHandle_t wordsq;                        // length 1, holds a SpinPool*
static SpinPool *g_heapPool = nullptr;       // active heap pool (nullptr = compiled-in default)

// ---- animation state ----
const int SW   = ANIM_W * ANIM_S;                 // 192
const int SH   = ANIM_H * ANIM_S;                 // 192
static uint16_t linebuf[ANIM_W * ANIM_S];
int xoff, yoff;
AnimId  curAnim  = ANIM_IDLE;
int     frame    = 0;
uint32_t lastFrame = 0;
// loop() must not draw before setup() finishes. If WiFi fails, setup returns
// early without a background, HUD or minis, and loop would otherwise paint the
// mascot on top of the black error screen. While false, loop leaves it alone.
static bool g_ready = false;
uint32_t revertAt  = 0;                            // when a transient mood ends (0 = none)
AnimId  returnAnim = ANIM_IDLE;                     // pose to return to after tickle/love

// ---- context-fullness indicator ----
// True when the last /status reported ctx% above CTX_BRAIN_THRESH. It only
// changes what "return to rest" resolves to (brain_full instead of idle); the
// reaction poses are untouched.
bool ctxHigh = false;

// ---- quota-exhausted indicator ----
// True when the last /status reported the 5h or weekly usage window at 100%.
// Like ctxHigh, it only changes what "return to rest" resolves to.
bool quotaFull = false;

static inline AnimId restAnim() {
  if (ctxHigh) return ANIM_BRAIN_FULL;
  if (quotaFull) return ANIM_IDLE_MUSIC;
  // Keep the current resting pose so repeated "return to rest" events do not
  // swap the mascot back and forth.
  return isIdlePose(curAnim) ? curAnim : ANIM_IDLE;
}

// ---- subagent mode ----
// agentActive counts real running subagents. While >0 the mini row is shown and
// the big pose is locked to ANIM_AGENTS so other events cannot overlap it.
int     agentActive = 0;
bool    agentMode   = false;

static void led(bool g, bool b) {                 // active-low
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

static void setAnim(AnimId id) {
  curAnim = id;
  frame = 0;
  lastFrame = 0;                                   // draw on the next tick
  const Anim &a = ANIMS[id];
  revertAt = a.transient ? millis() + HOLD_MS : 0;
  led(a.led_g, a.led_b);
  // ANIM_AGENTS draws only the top band, so clear the previous full-size
  // animation once on entry to leave a clean background for the minis.
  if (id == ANIM_AGENTS) tft.fillRect(xoff, yoff, SW, SH, tft.color565(BG_R, BG_G, BG_B));
  Serial.printf("[clawd] anim -> %s\n", a.name);
}

// Tools that mean "waiting on you", not "working". They arrive as a named
// tool.pre, so they can be recognised by name and shown as a calm pose.
static bool isWaitingTool(const char *t) {
  return !strcmp(t, "AskUserQuestion") || !strcmp(t, "ExitPlanMode");
}

// Event -> animation mapping (see the protocol doc). -1 = no change.
static int mapEvent(const Ev &e) {
  const char *k = e.k;
  if (!strcmp(k, "think"))          return e.on ? ANIM_THINK : ANIM_IDLE;
  if (!strcmp(k, "tool.pre"))       return isWaitingTool(e.tool) ? ANIM_ASK : ANIM_HACKING;
  if (!strcmp(k, "tool.post"))      return e.ok ? ANIM_IDLE : ANIM_OOPS;
  if (!strcmp(k, "git"))            return ANIM_HAPPY;
  if (!strcmp(k, "session.start"))  return ANIM_HAPPY;
  if (!strcmp(k, "agent.spawn"))    return ANIM_AGENTS;
  if (!strcmp(k, "agent.done"))     return -1;             // keep the pose; one mini leaves
  if (!strcmp(k, "compact"))        return ANIM_COMPACT;
  if (!strcmp(k, "wait"))           return ANIM_THINK;
  if (!strcmp(k, "prompt.submit"))  return ANIM_THINK;
  if (!strcmp(k, "session.stop"))   return ANIM_IDLE;
  if (!strcmp(k, "status"))         return -1;     // liveness only
  return ANIM_IDLE;
}

// ---- HUD: event -> short flavor text ----
// The action line shows a short flavor word picked per mood, never the actual
// command. A new word is picked only when the category changes, so the text
// stays put for the duration of a work phase.
enum HudCat { HC_IDLE, HC_THINK, HC_WORK, HC_HAPPY, HC_OOPS };
int g_hudCat = -1;

// WORK and THINK draw from Claude Code's real spinner pool (spinner_words.h).
// CC does not split those words by mood, so both categories share one pool.
// HAPPY/OOPS are device-specific — CC shows no spinner word on success or error.
// The pool can be replaced at runtime via POST /words.
static const char *const *g_spin  = SPINNER_WORDS_DEFAULT;   // active pool
static int                g_spinN = SPINNER_WORDS_DEFAULT_N;

static const char *HAPPY[] = { "Shipping!", "Nailed it!", "Boom!", "Committed!",
                               "Victory!", "High five!" };
static const char *OOPS[]  = { "Oops...", "Yikes...", "Uh-oh...", "Welp...",
                               "Awkward...", "Facepalm..." };

static const char *pick(const char *const *pool, int n) { return pool[esp_random() % n]; }

// Push the category to the HUD; the usage view's spinner line shows the same
// word. WORK/THINK pass the bare gerund with spin=true, so the device draws the
// pulsing mark and travelling dots itself (no "..." in the text). HAPPY/OOPS are
// static flavor text with their own punctuation.
static void setHudCat(HudCat cat) {
  if ((int)cat == g_hudCat) return;
  g_hudCat = cat;
  const char *txt = "";
  bool spin = false;
  switch (cat) {
    case HC_WORK:  txt = pick(g_spin, g_spinN); spin = true; break;
    case HC_THINK: txt = pick(g_spin, g_spinN); spin = true; break;
    case HC_HAPPY: txt = pick(HAPPY, 6); break;
    case HC_OOPS:  txt = pick(OOPS,  6); break;
    case HC_IDLE:  txt = ""; break;
  }
  hud.setAction(txt, spin);
  usage.setAction(txt, spin);
}

// Reflect the event on the HUD's spinner/flavor line.
static void updateHud(const Ev &e) {
  const char *k = e.k;
  if (!strcmp(k, "session.start")) { setHudCat(HC_HAPPY); return; }
  if (!strcmp(k, "session.stop"))  { setHudCat(HC_IDLE);  return; }
  if (!strcmp(k, "tool.post"))     { setHudCat(e.ok ? HC_IDLE : HC_OOPS); return; }
  if (!strcmp(k, "git"))           { setHudCat(HC_HAPPY); return; }
  if (!strcmp(k, "prompt.submit") || !strcmp(k, "compact") || !strcmp(k, "wait") ||
      (!strcmp(k, "think") && e.on)) { setHudCat(HC_THINK); return; }
  if (!strcmp(k, "think") && !e.on)  { setHudCat(HC_IDLE); return; }
  if (!strcmp(k, "agent.spawn")) { setHudCat(HC_WORK); return; }
  if (!strcmp(k, "agent.done"))  return;                 // a mini leaves; HUD unchanged
  if (!strcmp(k, "tool.pre")) { setHudCat(isWaitingTool(e.tool) ? HC_THINK : HC_WORK); return; }
}

static void drawFrame(AnimId id, int f) {
  const uint16_t *fr = ANIMS[id].frames[f];
  int rows = animPushRows(id);
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < ANIM_W; x++) {
      uint16_t c = fr[y * ANIM_W + x];
      for (int s = 0; s < ANIM_S; s++) linebuf[x * ANIM_S + s] = c;
    }
    for (int s = 0; s < ANIM_S; s++) tft.pushImage(xoff, yoff + y * ANIM_S + s, SW, 1, linebuf);
  }
}

// ---- WiFi ----
static bool connectWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", tft.width() / 2, tft.height() / 2, 2);
  WiFi.mode(WIFI_STA);
#if CLAWD_STATIC_IP
  // The device pins its own IP (a DHCP reservation does not work behind the extender).
  IPAddress ip(IP_LOCAL[0], IP_LOCAL[1], IP_LOCAL[2], IP_LOCAL[3]);
  IPAddress gw(IP_GATEWAY[0], IP_GATEWAY[1], IP_GATEWAY[2], IP_GATEWAY[3]);
  IPAddress mask(IP_SUBNET[0], IP_SUBNET[1], IP_SUBNET[2], IP_SUBNET[3]);
  IPAddress dns(IP_DNS[0], IP_DNS[1], IP_DNS[2], IP_DNS[3]);
  if (!WiFi.config(ip, gw, mask, dns))
    Serial.println("[clawd] WiFi.config failed -> falling back to DHCP");
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); Serial.print("."); }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] starting");

  pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  led(false, false);

  tft.init();
  tft.setRotation(1);                              // landscape 320x240
  tft.setSwapBytes(true);
  power.begin();                                   // takes the BL pin over to LEDC (after tft.init)

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  xoff = (tft.width()  - SW) / 2;                  // (320-192)/2 = 64
  yoff = (tft.height() - SH) / 2;                  // (240-192)/2 = 24

  evq = xQueueCreate(16, sizeof(Ev));
  statusq = xQueueCreate(1, sizeof(StatusMsg));
  wordsq  = xQueueCreate(1, sizeof(SpinPool*));

  if (!connectWiFi()) {
    // The router may just be slow to come up, so count down and restart. A
    // permanent problem (wrong password) loops here until the user reflashes
    // via install.sh. g_ready stays false, so loop() leaves this screen alone.
    Serial.println("[clawd] WiFi connect failed");   // install.sh greps this exact marker
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    const int cx = tft.width() / 2, cy = tft.height() / 2;
    for (int s = WIFI_RETRY_SECS; s > 0; s--) {
      tft.fillScreen(TFT_BLACK);
      tft.drawString("WiFi connection failed", cx, cy - 12, 2);
      char msg[40];
      snprintf(msg, sizeof(msg), "Retrying in %d s...", s);
      tft.setTextColor(tft.color565(150, 150, 162), TFT_BLACK);
      tft.drawString(msg, cx, cy + 12, 2);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      delay(1000);
    }
    Serial.println("[clawd] restarting (WiFi retry)...");
    ESP.restart();
  }
  WiFi.setSleep(true);                             // modem-sleep: association held, incoming packets wake us
  Serial.printf("[clawd] WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("clawd")) MDNS.addService("http", "tcp", 80);
  // NTP for the usage view's clock; it shows "--:--" until SNTP syncs.
  configTime(CLOCK_TZ_OFFSET_S, 0, "pool.ntp.org", "time.google.com");

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"fw\":\"1.0.0\",\"caps\":[\"anim\",\"led\",\"touch\",\"power\",\"hud\",\"status\"]}");
  });

  // POST /status (JSON: {m,ctx,h5,wk,h5r,wkr}) -> statusq (overwrite), 204.
  // Does not touch power management, so a status stream never keeps the device
  // awake; the latest value is kept and drawn on wake. h5r/wkr arrive already
  // formatted by the host ("2h32m"/"2d5h").
  auto *sHandler = new AsyncCallbackJsonWebHandler("/status", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonObject o = json.as<JsonObject>();
    StatusMsg s{};
    strlcpy(s.m, o["m"] | "", sizeof(s.m));
    s.ctx = o["ctx"] | -1;
    s.h5  = o["h5"]  | -1;
    s.wk  = o["wk"]  | -1;
    strlcpy(s.h5r, o["h5r"] | "", sizeof(s.h5r));
    strlcpy(s.wkr, o["wkr"] | "", sizeof(s.wkr));
    xQueueOverwrite(statusq, &s);
    req->send(204);
  });
  sHandler->setMethod(HTTP_POST);
  server.addHandler(sHandler);

  // POST /e (JSON) -> push onto the queue, 204
  auto *eHandler = new AsyncCallbackJsonWebHandler("/e", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonObject o = json.as<JsonObject>();
    Ev e{};
    strlcpy(e.k, o["k"] | "?", sizeof(e.k));
    JsonObject d = o["d"];
    strlcpy(e.g, d["g"] | "", sizeof(e.g));
    strlcpy(e.tool, d["tool"] | "", sizeof(e.tool));
    strlcpy(e.s, d["s"] | (d["tool"] | (d["op"] | "")), sizeof(e.s));
    e.ok  = d["ok"]  | true;
    e.on  = d["on"]  | false;
    e.ctx = d["ctx"] | -1;
    xQueueSend(evq, &e, 0);
    req->send(204);
  });
  eHandler->setMethod(HTTP_POST);
  server.addHandler(eHandler);

  // POST /words (JSON: {"w":["Cogitating","Herding",...]}) -> replace the spinner
  // pool. clawd:sync-spinner-words sends Claude Code's real list (or the user's
  // ~/.claude/clawd-spinner-words.txt). An empty or invalid body returns 400 and
  // leaves the current pool in place.
  auto *wHandler = new AsyncCallbackJsonWebHandler("/words", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonArray arr = json["w"].as<JsonArray>();
    if (arr.isNull()) { req->send(400, "application/json", "{\"err\":\"missing w[]\"}"); return; }

    // Pass 1: count valid strings and size the blob. Capped at 512.
    int n = 0; size_t blobLen = 0;
    for (JsonVariant v : arr) {
      const char *s = v.as<const char*>();
      if (!s || !*s) continue;
      if (n >= 512) break;
      n++; blobLen += strlen(s) + 1;
    }
    if (n == 0) { req->send(400, "application/json", "{\"err\":\"empty\"}"); return; }

    // heap: blob ('\0'-separated words) + idx (pointers into blob) + SpinPool.
    SpinPool *p = (SpinPool*)malloc(sizeof(SpinPool));
    char *blob  = (char*)malloc(blobLen);
    const char **idx = (const char**)malloc(sizeof(char*) * n);
    if (!p || !blob || !idx) { free(p); free(blob); free(idx);
      req->send(500, "application/json", "{\"err\":\"oom\"}"); return; }

    // Pass 2: copy.
    size_t o = 0; int i = 0;
    for (JsonVariant v : arr) {
      if (i >= n) break;
      const char *s = v.as<const char*>();
      if (!s || !*s) continue;
      size_t len = strlen(s) + 1;
      memcpy(blob + o, s, len);
      idx[i++] = blob + o;
      o += len;
    }
    p->blob = blob; p->idx = idx; p->n = n;

    // Free any pool loop() has not swapped in yet, so overwriting cannot leak it.
    SpinPool *stale = nullptr;
    if (xQueueReceive(wordsq, &stale, 0) == pdTRUE && stale) {
      free(stale->blob); free((void*)stale->idx); free(stale);
    }
    xQueueOverwrite(wordsq, &p);

    char msg[40]; snprintf(msg, sizeof(msg), "{\"ok\":true,\"n\":%d}", n);
    req->send(200, "application/json", msg);
  });
  wHandler->setMethod(HTTP_POST);
  server.addHandler(wHandler);

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[clawd] HTTP :80 up (/e /status /words /health)");

  tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));  // letterbox background
  setAnim(ANIM_IDLE);

  // HUD: initialise the bands and draw once. The status line fills in on /status.
  hud.begin(&tft);
  hud.setWifi(true, WiFi.RSSI());
  hud.setAction("");
  hud.render();

  minis.begin(&tft);
  usage.begin(&tft);
  g_ready = true;                                  // background + HUD + minis ready; loop may draw
}

// CPU side effects on power-state edges.
static void applyPowerEdge(PowerManager::State st) {
  if (st == PowerManager::SLEEP) {
    setCpuFrequencyMhz(CPU_HZ_SLEEP);
    Serial.println("[clawd] sleep: screen off, CPU 80MHz (WiFi up)");
  } else {                                          // ACTIVE / DIM -> full speed
    setCpuFrequencyMhz(CPU_HZ_ACTIVE);
  }
}

// Is the screen touched? Fills raw x/y. rawx drives gesture detection (relative
// movement, no calibration needed); rawx+rawy locate the corner button.
static bool readTouch(int &rawx, int &rawy) {
  if (!ts.tirqTouched() && !ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return false;
  rawx = p.x;
  rawy = p.y;
  return true;
}

// Is this raw touch inside the top-right corner button? The zone is generous,
// so rough calibration is enough.
static bool inCorner(int rawx, int rawy) {
  int sx = map(rawx, RAW_X_MIN, RAW_X_MAX, 0, 319);
  int sy = map(rawy, RAW_Y_MIN, RAW_Y_MAX, 0, 239);
  sx = constrain(sx, 0, 319);
  sy = constrain(sy, 0, 239);
  return sx >= 320 - UI_BTN_W && sy <= UI_BTN_H;
}

// Switch views. Both directions repaint from scratch; animation and event state
// are preserved.
static void toggleUsage() {
  uiUsage = !uiUsage;
  Serial.printf("[clawd] view -> %s\n", uiUsage ? "usage" : "normal");
  uiToggleAnimate = true;                            // slide the knob (only on this path)
  if (uiUsage) {
    usage.markAllDirty();
  } else {
    tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));
    hud.markAllDirty();
    minis.markAllDirty();
    lastFrame = 0;                                   // redraw the mascot on the next tick
  }
}

// Touch gesture state machine, called every loop. From the down/up edges and the
// raw x range during contact it distinguishes:
//   TG_DOUBLETAP = two short taps in a row (tickle)
//   TG_STROKE    = one contact with a wide horizontal sweep (petting)
enum TouchGesture { TG_NONE = 0, TG_DOUBLETAP, TG_STROKE };
static TouchGesture touchGesture(bool isTouched, int rawx) {
  static bool     contact = false;             // debounced contact in progress
  static uint32_t startMs = 0, seenMs = 0, lastTapMs = 0;
  static int      minX = 0, maxX = 0;
  static bool     settled = false;
  TouchGesture g = TG_NONE;
  uint32_t now = millis();

  if (isTouched) {
    seenMs = now;
    if (!contact) {                            // new contact
      contact = true; startMs = now; minX = maxX = rawx; settled = false;
    } else if (now - startMs >= TOUCH_SETTLE_MS) {   // track the x range after settling
      if (!settled) { minX = maxX = rawx; settled = true; }
      if (rawx < minX) minX = rawx;
      if (rawx > maxX) maxX = rawx;
    }
  } else if (contact && now - seenMs >= TOUCH_RELEASE_MS) {   // real release
    contact = false;
    uint32_t dur = seenMs - startMs;
    int range = maxX - minX;
    if (range >= STROKE_MIN_RAW) {             // wide sweep -> petting
      g = TG_STROKE; lastTapMs = 0;
    } else if (dur <= TAP_MAX_MS) {            // short and still -> tap
      if (lastTapMs && now - lastTapMs <= DOUBLETAP_MS) { g = TG_DOUBLETAP; lastTapMs = 0; }
      else lastTapMs = now;                    // first tap; wait for the second
    }
    // long and still (a resting finger) is ignored
    if (g || range || dur)
      Serial.printf("[touch] dur=%lu range=%d -> %s\n", (unsigned long)dur, range,
                    g == TG_STROKE ? "STROKE(love)" : g == TG_DOUBLETAP ? "DOUBLETAP(tickle)" : "tap/none");
  }
  return g;
}

void loop() {
  // setup() bailed early (e.g. no WiFi): nothing was initialised, so don't draw
  // over the error screen.
  if (!g_ready) { delay(200); return; }

  bool wasAsleep = power.asleep();
  int  rawx = 0, rawy = 0;
  bool isTouched = readTouch(rawx, rawy);

  // A touch STARTING in the corner switches views and never enters the gesture
  // machine, so double-tapping the toggle is not also read as a tickle. While
  // asleep the first touch only wakes the device.
  static bool     prevTouched  = false;
  static bool     cornerPress  = false;              // did this contact start in the corner
  static uint32_t lastToggleMs = 0;
  if (isTouched && !prevTouched) {                   // down edge
    cornerPress = inCorner(rawx, rawy);
    if (cornerPress && !wasAsleep && millis() - lastToggleMs >= UI_TOGGLE_DEBOUNCE_MS) {
      lastToggleMs = millis();
      toggleUsage();
    }
  } else if (!isTouched) {
    cornerPress = false;
  }
  prevTouched = isTouched;

  TouchGesture tg = touchGesture(isTouched && !cornerPress, rawx);

  // On a wake source, go full speed before anything else so UART and drawing
  // are not done at 80MHz.
  if (wasAsleep && (uxQueueMessagesWaiting(evq) > 0 || isTouched))
    setCpuFrequencyMhz(CPU_HZ_ACTIVE);

  // 1) drain the event queue — an incoming event counts as activity
  Ev e;
  while (xQueueReceive(evq, &e, 0) == pdTRUE) {
    power.notifyActivity();

    bool spawn  = !strcmp(e.k, "agent.spawn");
    bool done   = !strcmp(e.k, "agent.done");
    bool sbound = !strcmp(e.k, "session.start") || !strcmp(e.k, "session.stop");

    // Real subagent counter. The mini row appears on the first spawn and leaves
    // when the count returns to zero; session boundaries force it to zero.
    bool wasZero = (agentActive == 0);
    if (spawn)       { agentActive++; if (wasZero) minis.showAll(); }
    else if (done)   { if (agentActive > 0) agentActive--; if (agentActive == 0) minis.clear(); }
    else if (sbound) { agentActive = 0; minis.clear(); }
    agentMode = (agentActive > 0);

    // Terminating events clear busy; everything else sets it.
    //  - session.start/stop: no work in progress. (Counting start as busy would
    //    keep an idle session awake for 10 minutes.)
    //  - agent.done with the count at zero: a background agent finishing after
    //    the main turn's Stop is never followed by session.stop, so it must not
    //    re-arm busy.
    bool workDone = sbound || (done && agentActive == 0);
    power.setBusy(!workDone);

    // The big pose is locked to ANIM_AGENTS for the duration of agent mode;
    // outside it, the normal event mapping applies.
    if (spawn && wasZero) {
      setAnim(ANIM_AGENTS);
    } else if (done && agentActive == 0) {
      setAnim(restAnim()); setHudCat(HC_IDLE);
    } else if (!agentMode) {
      int id = mapEvent(e);
      if (id == ANIM_IDLE) id = restAnim();
      if (id >= 0 && id != (int)curAnim) setAnim((AnimId)id);
    }

    updateHud(e);
    Serial.printf("[clawd] event k=%s g=%s\n", e.k, e.g);
  }

  // 2) a touch counts as activity (and wakes from sleep)
  if (isTouched) power.notifyActivity();

  // 2b) gestures: double-tap -> tickle, stroke -> petting. Only while awake,
  // outside agent mode, and in the mascot view.
  if (tg != TG_NONE && !power.asleep() && !agentMode && !uiUsage) {
    // Remember the pose from before the touch so tickle/love returns to it
    // rather than to idle. Repeated touches must not overwrite that baseline.
    if (curAnim != ANIM_TICKLE && curAnim != ANIM_LOVE) returnAnim = curAnim;
    if (tg == TG_DOUBLETAP)   setAnim(ANIM_TICKLE);
    else if (tg == TG_STROKE) setAnim(ANIM_LOVE);
  }

  // 3) update power state and fade the backlight; apply side effects on edges
  if (power.tick()) {
    PowerManager::State st = power.state();
    applyPowerEdge(st);
    // Once the light drops, clawd dozes off.
    if (st == PowerManager::DIM) { setAnim(ANIM_SLEEP); setHudCat(HC_IDLE); minis.clear(); agentActive = 0; agentMode = false; }
    // Woke up: leave the sleep pose (unless an event already picked a new one).
    else if (st == PowerManager::ACTIVE && curAnim == ANIM_SLEEP) { setAnim(restAnim()); setHudCat(HC_IDLE); }
    // SLEEP: screen off, no drawing.
  }

  // just woke: repaint the background and the active view
  if (wasAsleep && !power.asleep()) {
    if (uiUsage) {
      usage.markAllDirty();
    } else {
      tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));
      hud.markAllDirty();
      minis.markAllDirty();
    }
  }

  // 4) a transient mood expired
  if (revertAt && millis() >= revertAt) {
    // tickle/love return to the pre-touch pose; other transients go to rest.
    bool touchAnim = (curAnim == ANIM_TICKLE || curAnim == ANIM_LOVE);
    AnimId back = touchAnim ? returnAnim : restAnim();
    setAnim(back);
    if (isIdlePose(back) || back == ANIM_BRAIN_FULL) setHudCat(HC_IDLE);
  }

  // 5) status queue. Consumed even while asleep (the buffer updates); drawing
  //    happens below, only while awake. Never touches power management.
  StatusMsg sm;
  if (xQueueReceive(statusq, &sm, 0) == pdTRUE) {
    hud.setStatus(sm.m, sm.ctx, sm.h5, sm.wk);
    hud.setReset(sm.h5r, sm.wkr);
    usage.setStatus(sm.h5, sm.wk, sm.h5r, sm.wkr);
    // Crossing the ctx% threshold swaps the resting pose immediately, without
    // waiting for the next event.
    bool nowHigh = sm.ctx >= CTX_BRAIN_THRESH;
    if (nowHigh != ctxHigh) {
      ctxHigh = nowHigh;
      if (!agentMode) {
        if (ctxHigh && isIdlePose(curAnim)) setAnim(ANIM_BRAIN_FULL);
        else if (!ctxHigh && curAnim == ANIM_BRAIN_FULL) setAnim(ANIM_IDLE);
      }
    }
    // Same edge-trigger for the 5h/weekly usage windows: either one hitting
    // 100% swaps the resting pose to idle_music; both clearing swaps it back.
    bool nowQuotaFull = (sm.h5 >= 100 || sm.wk >= 100);
    if (nowQuotaFull != quotaFull) {
      quotaFull = nowQuotaFull;
      if (!agentMode && !ctxHigh) {
        if (quotaFull && isIdlePose(curAnim)) setAnim(ANIM_IDLE_MUSIC);
        else if (!quotaFull && curAnim == ANIM_IDLE_MUSIC) setAnim(ANIM_IDLE);
      }
    }
  }

  // Activate a spinner pool posted to /words. Swapping here keeps g_spin single-
  // threaded; the previous heap pool is freed at the same time.
  SpinPool *np;
  if (xQueueReceive(wordsq, &np, 0) == pdTRUE && np) {
    g_spin = np->idx; g_spinN = np->n;
    if (g_heapPool) { free(g_heapPool->blob); free((void*)g_heapPool->idx); free(g_heapPool); }
    g_heapPool = np;
    Serial.printf("[clawd] spinner pool updated: %d words\n", np->n);
  }

  // 6) refresh the WiFi signal every 4 s; setWifi redraws only when the bar
  //    count changes, so a fluctuating RSSI costs no SPI traffic. If the
  //    association was dropped (e.g. long modem-sleep + extender roaming/
  //    DHCP lease loss), kick a reconnect — otherwise the device sits
  //    disconnected forever and stops receiving events until power-cycled.
  static uint32_t lastWifiPoll = 0;
  if (millis() - lastWifiPoll >= 4000) {
    lastWifiPoll = millis();
    bool c = (WiFi.status() == WL_CONNECTED);
    hud.setWifi(c, c ? WiFi.RSSI() : -127);
    if (!c) {
      Serial.println("[clawd] WiFi disconnected -> reconnecting");
      WiFi.reconnect();
    }
  }

  // 7) draw — nothing while asleep. In usage mode the quota view replaces the
  // mascot/HUD/minis; the animation does not advance but its state is kept.
  if (!power.asleep()) {
    if (uiUsage) {
      usage.render();
    } else {
      uint32_t now = millis();
      if (now - lastFrame >= ANIMS[curAnim].interval) {
        lastFrame = now;
        drawFrame(curAnim, frame);
        frame = (frame + 1) % ANIMS[curAnim].count;
      }
      hud.render();
      minis.tick();
    }
  }

  delay(power.asleep() ? 40 : 5);                   // idle longer while asleep
}
