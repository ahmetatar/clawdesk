// clawd — ana uygulama (CYD / ESP32-2432S028R), LANDSCAPE.
//
// Fiziksel Claude Code maskotu: WiFi'dan POST /e olayi alir, 5 ifade
// animasyonuyla (idle/hacking/happy/think/oops) tepki verir.
//
// ILK OZELLIK — GUC YONETIMI (bkz. power.h):
//   30 sn olaysiz -> arka isik %11'e kisilir (dim), animasyon doner
//   120 sn olaysiz -> ekran soner, animasyon durur, CPU 80MHz, WiFi modem-sleep
//   POST /e veya dokunma -> aninda tam parlaklik + 240MHz + animasyon (uyanma)
//
// MIMARI: AsyncWebServer callback'leri AYRI task'ta calisir; SPI'ye (ekran)
// oradan DOKUNULMAZ. /e callback'i olayi thread-safe kuyruga push eder;
// tum cizim ve dokunmatik yalniz loop()'ta olur.

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

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(T_CS);
AsyncWebServer server(80);
PowerManager power;
Hud hud;
MiniFleet minis;   // alt-agent (Task) gorsellestirme: yan sutunlarda gezinen kucuk clawd'lar

// ---- olay kuyrugu (07'den) ----
struct Ev { char k[24]; char g[10]; char s[48]; char tool[16]; bool ok; bool on; int ctx; };
QueueHandle_t evq;

// ---- status kuyrugu: Claude Code statusLine ozeti (POST /status) ----
// Ayri kuyruk cunku status GUC yonetimini ETKILEMEMELI (uyku sayacini sifirlamaz,
// cihazi uyandirmaz). Uzunluk 1 + overwrite: hep en son deger tutulur.
struct StatusMsg { char m[12]; int ctx; int h5; int wk; };
QueueHandle_t statusq;

// ---- animasyon durumu ----
const int SW   = ANIM_W * ANIM_S;                 // 192
const int SH   = ANIM_H * ANIM_S;                 // 192
static uint16_t linebuf[ANIM_W * ANIM_S];
int xoff, yoff;
AnimId  curAnim  = ANIM_IDLE;
int     frame    = 0;
uint32_t lastFrame = 0;
uint32_t revertAt  = 0;                            // gecici ifade -> idle zamani (0 = yok)

// ---- alt-agent modu ----
// agentActive = o an calisan GERCEK alt-agent sayisi (spawn++/done--). >0 iken "agent modu":
// ekranda hep 4 mini gosterilir ve buyuk poz ANIM_AGENTS'te KILITLI kalir (tool.pre/hacking
// vb. mini'lerin ustune binmesin). Sayac 0'a inince (is bitince) 4 mini de kaybolur.
int     agentActive = 0;
bool    agentMode   = false;

static void led(bool g, bool b) {                 // active-low
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

static void setAnim(AnimId id) {
  curAnim = id;
  frame = 0;
  lastFrame = 0;                                   // bir sonraki tick'te hemen ciz
  const Anim &a = ANIMS[id];
  revertAt = a.transient ? millis() + HOLD_MS : 0;
  led(a.led_g, a.led_b);
  // ANIM_AGENTS yalniz ust bandi cizer; alt bant (mini zemini) her frame yenilenmez.
  // Geciste onceki tam-boy anim'in kalintisini merkezden bir kez sil ki zemin temiz BG kalsin.
  if (id == ANIM_AGENTS) tft.fillRect(xoff, yoff, SW, SH, tft.color565(BG_R, BG_G, BG_B));
  Serial.printf("[clawd] anim -> %s\n", a.name);
}

// "Kullanici bekleniyor" tool'lari: Claude soru/onay soruyor -> calisiyor DEGIL,
// SENI bekliyor. Bunlar da isimli tool.pre gonderir (interrupt'in aksine), o yuzden
// ADIYLA tespit edip hacking yerine sakin think pozuna alabiliriz.
static bool isWaitingTool(const char *t) {
  return !strcmp(t, "AskUserQuestion") || !strcmp(t, "ExitPlanMode");
}

// olay -> animasyon eslemesi (protokol 9. bolum). -1 = degisiklik yok.
static int mapEvent(const Ev &e) {
  const char *k = e.k;
  if (!strcmp(k, "think"))          return e.on ? ANIM_THINK : ANIM_IDLE;
  if (!strcmp(k, "tool.pre"))       return isWaitingTool(e.tool) ? ANIM_ASK : ANIM_HACKING;
  if (!strcmp(k, "tool.post"))      return e.ok ? ANIM_IDLE : ANIM_OOPS;
  if (!strcmp(k, "git"))            return ANIM_HAPPY;
  if (!strcmp(k, "session.start"))  return ANIM_HAPPY;
  if (!strcmp(k, "agent.spawn"))    return ANIM_AGENTS;    // alt-agent: maskot yukari kayip mini'lere bakar
  if (!strcmp(k, "agent.done"))     return -1;             // alt-agent bitti: pozu bozma, mini eksilir
  if (!strcmp(k, "compact"))        return ANIM_THINK;
  if (!strcmp(k, "wait"))           return ANIM_THINK;
  if (!strcmp(k, "prompt.submit"))  return ANIM_THINK;
  if (!strcmp(k, "session.stop"))   return ANIM_IDLE;
  if (!strcmp(k, "status"))         return -1;     // sadece canli tut, animasyonu bozma
  return ANIM_IDLE;
}

// ---- HUD: olay -> kisa flavor metni (Feature 1 + 3) ----
// Aksiyon metni = KOMUT DEGIL, kisa/eglenceli "flavor" kelime (status-line tarzi).
// Karmasik komutlari gostermek yerine mood'a gore bir havuzdan rastgele secilir.
// Titremeyi onlemek icin: kategori DEGISMEDIKCE yeni kelime secilmez (ayni is
// fazinda kelime sabit kalir).
enum HudCat { HC_IDLE, HC_THINK, HC_WORK, HC_HAPPY, HC_OOPS };
int g_hudCat = -1;

static const char *WORK[]  = { "Crafting...", "Tinkering...", "Hacking...", "Wrangling...",
                               "Conjuring...", "Summoning...", "Brewing...", "Forging...",
                               "Cooking...", "Noodling...", "Fiddling...", "Hustling..." };
static const char *THINK[] = { "Pondering...", "Thinking...", "Musing...", "Scheming...",
                               "Plotting...", "Percolating...", "Daydreaming...", "Wondering..." };
static const char *HAPPY[] = { "Shipping!", "Nailed it!", "Boom!", "Committed!",
                               "Victory!", "High five!" };
static const char *OOPS[]  = { "Oops...", "Yikes...", "Uh-oh...", "Welp...",
                               "Awkward...", "Facepalm..." };

static const char *pick(const char *const *pool, int n) { return pool[esp_random() % n]; }

// Tool adini sag-alt icin kisalt: mcp__server__method -> son segment.
static const char *niceTool(const char *t) {
  if (!strncmp(t, "mcp__", 5)) { const char *p = strrchr(t, '_'); return (p && p[1]) ? p + 1 : "mcp"; }
  return t;
}

// Kategoriyi HUD'a yansit. Kategori degismediyse dokunma (kelime sabit kalir).
static void setHudCat(HudCat cat) {
  if ((int)cat == g_hudCat) return;
  g_hudCat = cat;
  switch (cat) {
    case HC_WORK:  hud.setAction(pick(WORK,  12)); break;
    case HC_THINK: hud.setAction(pick(THINK,  8)); break;
    case HC_HAPPY: hud.setAction(pick(HAPPY,  6)); break;
    case HC_OOPS:  hud.setAction(pick(OOPS,   6)); break;
    case HC_IDLE:  hud.setAction(""); break;
  }
}

// Olayi HUD'a yansit: sol-alt flavor metni + sag-alt calisan tool adi.
// tool ismi tool.pre'de set edilir; dusunme/bosta temizlenir; tool.post'ta kalir
// (biten arac kisa sure gorunur, sonraki tool.pre hemen degistirir).
static void updateHud(const Ev &e) {
  const char *k = e.k;
  if (!strcmp(k, "session.start")) { setHudCat(HC_HAPPY); hud.setTool(""); return; }
  if (!strcmp(k, "session.stop"))  { setHudCat(HC_IDLE);  hud.setTool(""); return; }
  if (!strcmp(k, "tool.post"))     { setHudCat(e.ok ? HC_IDLE : HC_OOPS); if (e.ok) hud.setTool(""); return; }
  if (!strcmp(k, "git"))           { setHudCat(HC_HAPPY); return; }
  if (!strcmp(k, "prompt.submit") || !strcmp(k, "compact") || !strcmp(k, "wait") ||
      (!strcmp(k, "think") && e.on)) { setHudCat(HC_THINK); hud.setTool(""); return; }
  if (!strcmp(k, "think") && !e.on)  { setHudCat(HC_IDLE); hud.setTool(""); return; }
  if (!strcmp(k, "agent.spawn")) { setHudCat(HC_WORK); hud.setTool("agent"); return; }
  if (!strcmp(k, "agent.done"))  return;                 // mini eksilir; HUD'a dokunma
  if (!strcmp(k, "tool.pre")) {
    if (isWaitingTool(e.tool)) { setHudCat(HC_THINK); hud.setTool("waiting..."); return; }
    setHudCat(HC_WORK);
    hud.setTool(niceTool(e.tool[0] ? e.tool : e.g));
    return;
  }
}

static void drawFrame(AnimId id, int f) {
  const uint16_t *fr = ANIMS[id].frames[f];
  int rows = animPushRows(id);                      // ANIM_AGENTS: yalniz ust bant (alt = mini zemini)
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
  tft.drawString("WiFi baglaniyor...", tft.width() / 2, tft.height() / 2, 2);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(300); Serial.print("."); }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] ana uygulama basliyor");

  pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  led(false, false);

  tft.init();
  tft.setRotation(1);                              // LANDSCAPE 320x240
  tft.setSwapBytes(true);
  power.begin();                                   // BL pinini LEDC'ye devralir (tft.init SONRASI)

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  xoff = (tft.width()  - SW) / 2;                  // (320-192)/2 = 64
  yoff = (tft.height() - SH) / 2;                  // (240-192)/2 = 24

  evq = xQueueCreate(16, sizeof(Ev));
  statusq = xQueueCreate(1, sizeof(StatusMsg));

  if (!connectWiFi()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WiFi BAGLANAMADI", tft.width() / 2, tft.height() / 2, 2);
    Serial.println("[clawd] WiFi BAGLANAMADI");
    return;
  }
  WiFi.setSleep(true);                             // modem-sleep: association korunur, gelen paket uyandirir
  Serial.printf("[clawd] WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("clawd")) MDNS.addService("http", "tcp", 80);

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"fw\":\"1.0.0\",\"caps\":[\"anim\",\"led\",\"touch\",\"power\",\"hud\",\"status\"]}");
  });

  // POST /status (JSON: {m,ctx,h5,wk}) -> statusq (overwrite), 204.
  // Claude Code statusLine ozeti. GUC yonetimine dokunmaz (notifyActivity YOK) ->
  // status akisi cihazi uyanik tutmaz; uykudayken son deger saklanir, uyaninca cizilir.
  auto *sHandler = new AsyncCallbackJsonWebHandler("/status", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonObject o = json.as<JsonObject>();
    StatusMsg s{};
    strlcpy(s.m, o["m"] | "", sizeof(s.m));
    s.ctx = o["ctx"] | -1;
    s.h5  = o["h5"]  | -1;
    s.wk  = o["wk"]  | -1;
    xQueueOverwrite(statusq, &s);
    req->send(204);
  });
  sHandler->setMethod(HTTP_POST);
  server.addHandler(sHandler);

  // POST /e (JSON) -> kuyruga push, 204
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

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "yok"); });
  server.begin();
  Serial.println("[clawd] HTTP :80 ayakta (/e /health)");

  tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));  // fume letterbox
  setAnim(ANIM_IDLE);

  // HUD: bantlari baslat + ilk cizim. Ust bant statusLine ozeti (POST /status ile dolar).
  hud.begin(&tft);
  hud.setAction("");                               // idle: sol-alt bos
  hud.render();

  minis.begin(&tft);                               // yan sutun mini-clawd filosu (bos baslar)
}

// Uyku durumu kenarlarinda CPU/uyandirma yan etkileri.
static void applyPowerEdge(PowerManager::State st) {
  if (st == PowerManager::SLEEP) {
    setCpuFrequencyMhz(CPU_HZ_SLEEP);
    Serial.println("[clawd] uyku: ekran kapali, CPU 80MHz (WiFi ayakta)");
  } else {                                          // ACTIVE / DIM -> tam hiz
    setCpuFrequencyMhz(CPU_HZ_ACTIVE);
  }
}

// Dokunma var mi? (uyandirma icin sadece basinc yeterli — kalibrasyon gerekmez)
static bool touched() {
  if (!ts.tirqTouched() && !ts.touched()) return false;
  TS_Point p = ts.getPoint();
  return p.z >= 200;
}

void loop() {
  bool wasAsleep = power.asleep();
  bool isTouched = touched();

  // Uyandirma kaynagi (event/dokunma) varsa ve uykudaysak CPU'yu HERSEYDEN ONCE
  // tam hiza al: uyku 80MHz iken UART/cizim bozulmasin, olay tam hizda islensin.
  if (wasAsleep && (uxQueueMessagesWaiting(evq) > 0 || isTouched))
    setCpuFrequencyMhz(CPU_HZ_ACTIVE);

  // 1) olay kuyrugunu bosalt — gelen olay = aktivite (WiFi ile uyanma buradan)
  Ev e;
  while (xQueueReceive(evq, &e, 0) == pdTRUE) {
    power.notifyActivity();
    // Claude mesgul mu? session.start ve session.stop = calisma YOK -> busy kapali.
    // session.start'i busy sayarsak, olaysiz acilan / (/clear'lanan) oturumda cihaz
    // 10 dk uyanik kalir ve 30s dim / 120s uyku baypas olur. Gercek "mesgul" ancak
    // prompt/tool/think ile baslar, session.stop ile biter.
    power.setBusy(strcmp(e.k, "session.start") != 0 && strcmp(e.k, "session.stop") != 0);

    bool spawn  = !strcmp(e.k, "agent.spawn");
    bool done   = !strcmp(e.k, "agent.done");
    bool sbound = !strcmp(e.k, "session.start") || !strcmp(e.k, "session.stop");

    // Gercek alt-agent sayaci. Ilk spawn'da 4 mini BIRDEN acilir; sayac 0'a
    // inince (is bitince) 4'u de kaybolur. Oturum sinirinda kesin sifir.
    bool wasZero = (agentActive == 0);
    if (spawn)       { agentActive++; if (wasZero) minis.showAll(); }
    else if (done)   { if (agentActive > 0) agentActive--; if (agentActive == 0) minis.clear(); }
    else if (sbound) { agentActive = 0; minis.clear(); }
    agentMode = (agentActive > 0);

    // Buyuk poz: agent modu boyunca ANIM_AGENTS'te KILITLI (tool.pre/hacking vb.
    // mini'lerin ustune binmesin). Ilk spawn -> agents; son done -> idle; diger
    // olaylar agent modunda pozu DEGISTIRMEZ; mod disinda normal esleme.
    if (spawn && wasZero) {
      setAnim(ANIM_AGENTS);
    } else if (done && agentActive == 0) {
      setAnim(ANIM_IDLE); setHudCat(HC_IDLE); hud.setTool("");
    } else if (!agentMode) {
      int id = mapEvent(e);
      if (id >= 0 && id != (int)curAnim) setAnim((AnimId)id);
    }

    updateHud(e);                                      // HUD koseleri her olayda guncellenir
    Serial.printf("[clawd] olay k=%s g=%s\n", e.k, e.g);
  }

  // 2) dokunma = aktivite (uykuda da uyandirir)
  if (isTouched) power.notifyActivity();

  // 3) guc durumunu guncelle + arka isik fade; durum degistiyse yan etki uygula
  if (power.tick()) {
    PowerManager::State st = power.state();
    applyPowerEdge(st);                              // CPU frekansi
    // Isik dustugunde clawd uyuklama pozuna gecer (kapali gozler + zzZZ).
    // (busy-gate mesgulken kismayi engeller -> normalde burada mini olmaz; yine de temizle.)
    if (st == PowerManager::DIM) { setAnim(ANIM_SLEEP); setHudCat(HC_IDLE); hud.setTool(""); minis.clear(); agentActive = 0; agentMode = false; }
    // Uyandi: uyku pozundaysak idle'a don (bir olay yeni anim atadiysa ona dokunma).
    else if (st == PowerManager::ACTIVE && curAnim == ANIM_SLEEP) { setAnim(ANIM_IDLE); setHudCat(HC_IDLE); hud.setTool(""); }
    // SLEEP: ekran kapali, cizim yok.
  }

  // uykudan yeni ciktiysak: fume zemini tazele (son frame duruyordu) + HUD'u yeniden ciz
  if (wasAsleep && !power.asleep()) {
    tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));
    hud.markAllDirty();
    minis.markAllDirty();                            // temiz zemine yeniden cizsinler
  }

  // 4) gecici ifade suresi doldu -> idle (flavor + tool adini da temizle)
  if (revertAt && millis() >= revertAt) { setAnim(ANIM_IDLE); setHudCat(HC_IDLE); hud.setTool(""); }

  // 5) Status kuyrugu: Claude Code statusLine ozeti. GUC yonetimine DOKUNMAZ
  //    (ayri kuyruk, notifyActivity yok) -> status akisi cihazi uyanik tutmaz.
  //    Uykuda da tuketilir (buffer guncellenir); cizim yalniz uyanikken (asagida).
  StatusMsg sm;
  if (xQueueReceive(statusq, &sm, 0) == pdTRUE) hud.setStatus(sm.m, sm.ctx, sm.h5, sm.wk);

  // 6) animasyon + HUD — uykuda cizme (ekran zaten kapali, CPU'yu bosa harcama)
  if (!power.asleep()) {
    uint32_t now = millis();
    if (now - lastFrame >= ANIMS[curAnim].interval) {
      lastFrame = now;
      drawFrame(curAnim, frame);
      frame = (frame + 1) % ANIMS[curAnim].count;
    }
    hud.render();                                    // yalniz kirli koseler cizilir
    minis.tick();                                    // yan sutunlarda alt-agent mini'leri
  }

  delay(power.asleep() ? 40 : 5);                   // uykuda daha uzun bekle (guc)
}
