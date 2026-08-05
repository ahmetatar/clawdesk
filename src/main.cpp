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
#include "usage.h"
#include "spinner_words.h"

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(T_CS);
AsyncWebServer server(80);
PowerManager power;
Hud hud;
MiniFleet minis;   // alt-agent (Task) gorsellestirme: yan sutunlarda gezinen kucuk clawd'lar
UsageScreen usage; // kota gorunumu (sag-ust kose butonuyla degistirilir)

// ---- gorunum modu ----
// false = normal (maskot + HUD), true = usage (kota kartlari). Sag-ust kosede
// BASLAYAN kisa dokunus degistirir. Usage modunda maskot/HUD/mini cizilmez;
// olay isleme ve guc yonetimi AYNEN calisir (donuste kaldigi yerden devam).
static bool uiUsage = false;

// ---- olay kuyrugu (07'den) ----
struct Ev { char k[24]; char g[10]; char s[48]; char tool[16]; bool ok; bool on; int ctx; };
QueueHandle_t evq;

// ---- status kuyrugu: Claude Code statusLine ozeti (POST /status) ----
// Ayri kuyruk cunku status GUC yonetimini ETKILEMEMELI (uyku sayacini sifirlamaz,
// cihazi uyandirmaz). Uzunluk 1 + overwrite: hep en son deger tutulur.
struct StatusMsg { char m[12]; int ctx; int h5; int wk; char h5r[10]; char wkr[10]; };
QueueHandle_t statusq;

// ---- spinner havuzu overwrite (POST /words) ----
// Claude Code'un GERCEK spinner listesi cihaza yansitilabilsin diye (clawd:sync-spinner-words).
// Async callback listeyi HEAP'te bir SpinPool'a ayristirip pointer'i wordsq'ya koyar;
// takas + eski havuzu free() loop()'ta yapilir -> g_spin'e yalniz loop task dokunur
// (pick de loop'ta), boylece kilitsiz thread-safe. (SPI'ye async'ten dokunulmaz kurali korunur.)
struct SpinPool { char *blob; const char **idx; int n; };
QueueHandle_t wordsq;                        // uzunluk 1, SpinPool* tutar
static SpinPool *g_heapPool = nullptr;       // o an aktif heap havuz (nullptr = derleme-ici varsayilan)

// ---- animasyon durumu ----
const int SW   = ANIM_W * ANIM_S;                 // 192
const int SH   = ANIM_H * ANIM_S;                 // 192
static uint16_t linebuf[ANIM_W * ANIM_S];
int xoff, yoff;
AnimId  curAnim  = ANIM_IDLE;
int     frame    = 0;
uint32_t lastFrame = 0;
// setup() tam bitmeden loop() cizim yapmasin. WiFi baglanamayip setup erken return
// ederse (fume zemin + HUD + minis KURULMAZ) loop yine de calisir; bu bayrak olmadan
// drawFrame maskotu SIYAH hata ekraninin ustune fume-kare olarak basardi (uyku/uyanma
// fillScreen'i gelene kadar cirkin dururdu). false iken loop yalniz hata ekranini korur.
static bool g_ready = false;
uint32_t revertAt  = 0;                            // gecici ifade -> donus zamani (0 = yok)
AnimId  returnAnim = ANIM_IDLE;                     // dokunmatik tickle/love bitince DONULECEK poz
                                                   // (dokunus oncesi durum: think/hacking/idle...)

// ---- baglam-doluluk (ctx%) gostergesi ----
// ctxHigh = son /status'ta ctx% CTX_BRAIN_THRESH (statusLine "kirmizi" esigiyle ayni)
// UZERINDE mi. "Sakin dinlenme" pozu bu bayrağa gore ANIM_IDLE yerine ANIM_BRAIN_FULL
// olur (restAnim()) — DIM/SLEEP'e gecmeden ONCE bir uyari katmani. Gercek reaksiyon
// pozlarini (hacking/happy/oops/ask/agents) BOZMAZ, yalniz "dinlenmeye donus" hedefini degistirir.
bool ctxHigh = false;

// ---- dinlenme pozu (idle havuzu) ----
// Bekleme maskotu tek degil: IDLE_POOL'dan (anims.h) AGIRLIKLI rastgele biri
// gosterilir — sade idle %85, kulaklikli muzik pozu %15 (uniform cekilis %50
// yapiyordu, muzik maskotu ekranda fazla goruluyordu).
// STICKY: zaten bir idle pozundaysak yeniden ZAR ATMAYIZ -> ust uste gelen
// "dinlenmeye don" olaylari maskotu ekranda takas edip titretmez; poz ancak
// gercek bir reaksiyon pozundan (hacking/happy/think...) donunce degisir.
static inline AnimId pickPose(const PosePick *pool, uint8_t n, uint16_t total) {
  uint16_t r = (uint16_t)(esp_random() % total);
  for (uint8_t i = 0; i < n - 1; i++) {
    if (r < pool[i].weight) return pool[i].id;
    r -= pool[i].weight;
  }
  return pool[n - 1].id;                               // son uye: kalan tum agirlik
}
static inline AnimId pickIdle() { return pickPose(IDLE_POOL, IDLE_POOL_N, idlePoolTotal()); }

// ---- "calisiyor" pozu (WORK_POOL: klavye %75 / tava %25) ----
// Cekilis HER tool.pre'de yapilmaz: tool.pre -> tool.post arasi kisa oldugu ve
// tool cagrilari pes pese geldigi icin her seferinde zar atmak maskotu klavye ile
// tava arasinda saniyede bir takas ettirir. Bunun yerine YAPISKAN pencere: son
// calisma pozunun uzerinden WORK_STICKY_MS'den az gectiyse ayni poz surdurulur
// (bir istem boyunca tek maskot), uzun bir sessizlikten sonraki ilk tool yeni zar.
static const uint32_t WORK_STICKY_MS = 30000;
static AnimId  workPose   = ANIM_HACKING;
static uint32_t workPoseAt = 0;                        // 0 = henuz hic secilmedi
static inline AnimId pickWork() {
  uint32_t now = millis();
  if (!workPoseAt || now - workPoseAt >= WORK_STICKY_MS)
    workPose = pickPose(WORK_POOL, WORK_POOL_N, workPoolTotal());
  workPoseAt = now;                                    // pencereyi tazele
  return workPose;
}

static inline AnimId restAnim() {
  if (ctxHigh) return ANIM_BRAIN_FULL;                // context kritik: beyin uyarisi
  return isIdlePose(curAnim) ? curAnim : pickIdle();
}

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
  if (!strcmp(k, "tool.pre"))       return isWaitingTool(e.tool) ? ANIM_ASK : pickWork();
  if (!strcmp(k, "tool.post"))      return e.ok ? ANIM_IDLE : ANIM_OOPS;
  if (!strcmp(k, "git"))            return ANIM_HAPPY;
  if (!strcmp(k, "session.start"))  return ANIM_HAPPY;
  if (!strcmp(k, "agent.spawn"))    return ANIM_AGENTS;    // alt-agent: maskot yukari kayip mini'lere bakar
  if (!strcmp(k, "agent.done"))     return -1;             // alt-agent bitti: pozu bozma, mini eksilir
  if (!strcmp(k, "compact"))        return ANIM_COMPACT;   // beyin + yildizlar: zihin temizleniyor
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

// WORK ve THINK ("clawd calisiyor/dusunuyor") -> Claude Code'un GERCEK spinner
// havuzundan beslenir (spinner_words.h, ~178 gerund). CC bu kelimeleri mood'a gore
// ayirmaz (hepsi "mesgulum" tek havuzu), o yuzden iki durum da ayni havuzdan secer.
// HAPPY/OOPS ise CIHAZA OZEL (CC basaride/hatada spinner kelimesi gostermez) -> kalir.
// Spinner havuzu calisma-zamaninda POST /words ile degistirilebilir (asagi bak).
static const char *const *g_spin  = SPINNER_WORDS_DEFAULT;   // aktif havuz (varsayilan: derleme-ici)
static int                g_spinN = SPINNER_WORDS_DEFAULT_N;

static const char *HAPPY[] = { "Shipping!", "Nailed it!", "Boom!", "Committed!",
                               "Victory!", "High five!" };
static const char *OOPS[]  = { "Oops...", "Yikes...", "Uh-oh...", "Welp...",
                               "Awkward...", "Facepalm..." };

static const char *pick(const char *const *pool, int n) { return pool[esp_random() % n]; }

// Spinner kelimeleri "...'siz" gelir (CC gerund'lari: "Cogitating"). HUD'da spinner
// hissi icin sonuna "..." ekleyip statik tampona yaz (setAction kopyalar).
static const char *pickSpin() {
  static char buf[40];
  snprintf(buf, sizeof(buf), "%s...", pick(g_spin, g_spinN));
  return buf;
}

// Kategoriyi HUD'a yansit. Kategori degismediyse dokunma (kelime sabit kalir).
// Usage ekraninin alt spinner satiri da AYNI kelimeyi gosterir (tek kaynak).
static void setHudCat(HudCat cat) {
  if ((int)cat == g_hudCat) return;
  g_hudCat = cat;
  const char *txt = "";
  switch (cat) {
    case HC_WORK:  txt = pickSpin(); break;
    case HC_THINK: txt = pickSpin(); break;
    case HC_HAPPY: txt = pick(HAPPY, 6); break;
    case HC_OOPS:  txt = pick(OOPS,  6); break;
    case HC_IDLE:  txt = ""; break;
  }
  hud.setAction(txt);
  usage.setAction(txt);
}

// Olayi HUD ust satirina (spinner/flavor) yansit. (Tool adi satiri KALDIRILDI.)
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
  if (!strcmp(k, "agent.done"))  return;                 // mini eksilir; HUD'a dokunma
  if (!strcmp(k, "tool.pre")) { setHudCat(isWaitingTool(e.tool) ? HC_THINK : HC_WORK); return; }
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
  tft.drawString("Connecting to WiFi...", tft.width() / 2, tft.height() / 2, 2);
  WiFi.mode(WIFI_STA);
#if CLAWD_STATIC_IP
  // Cihaz IP'yi kendisi sabitler (extender arkasinda DHCP reservation calismaz).
  IPAddress ip(IP_LOCAL[0], IP_LOCAL[1], IP_LOCAL[2], IP_LOCAL[3]);
  IPAddress gw(IP_GATEWAY[0], IP_GATEWAY[1], IP_GATEWAY[2], IP_GATEWAY[3]);
  IPAddress mask(IP_SUBNET[0], IP_SUBNET[1], IP_SUBNET[2], IP_SUBNET[3]);
  IPAddress dns(IP_DNS[0], IP_DNS[1], IP_DNS[2], IP_DNS[3]);
  if (!WiFi.config(ip, gw, mask, dns))
    Serial.println("[clawd] WiFi.config BASARISIZ -> DHCP'ye donuluyor");
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
  wordsq  = xQueueCreate(1, sizeof(SpinPool*));

  if (!connectWiFi()) {
    // Router henuz acilmamis / gecici parazit olabilir -> geri sayarak otomatik yeniden
    // dene (ESP.restart cihazi bastan baslatir, setup tekrar connectWiFi cagirir).
    // Kalici sorunsa (yanlis sifre) her turda geri sayip yeniden dener; kullanici
    // install.sh ile duzeltip flaslayana kadar dongude kalir. g_ready hala false
    // oldugundan loop() bu ekrana dokunmaz.
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
    Serial.println("[clawd] yeniden baslatiliyor (WiFi retry)...");
    ESP.restart();
  }
  WiFi.setSleep(true);                             // modem-sleep: association korunur, gelen paket uyandirir
  Serial.printf("[clawd] WiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("clawd")) MDNS.addService("http", "tcp", 80);
  // NTP saat (usage ekranindaki buyuk saat icin). SNTP arka planda senkronlar ve
  // periyodik tazeler; senkron gelene kadar usage ekrani "--:--" gosterir.
  configTime(CLOCK_TZ_OFFSET_S, 0, "pool.ntp.org", "time.google.com");

  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"fw\":\"1.0.0\",\"caps\":[\"anim\",\"led\",\"touch\",\"power\",\"hud\",\"status\"]}");
  });

  // POST /status (JSON: {m,ctx,h5,wk,h5r,wkr}) -> statusq (overwrite), 204.
  // Claude Code statusLine ozeti. GUC yonetimine dokunmaz (notifyActivity YOK) ->
  // status akisi cihazi uyanik tutmaz; uykudayken son deger saklanir, uyaninca cizilir.
  // h5r/wkr: host'ta ONCEDEN bicimlendirilmis reset geri sayimi ("2h32m"/"2d5h"); bos -> "-".
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

  // POST /words (JSON: {"w":["Cogitating","Herding",...]}) -> spinner havuzunu DEGISTIR.
  // clawd:sync-spinner-words komutu Claude Code'un gercek listesini (ya da kullanicinin
  // ~/.claude/clawd-spinner-words.txt ozelini) buraya yollar. Kelimeler HEAP'te tek bir
  // SpinPool'a kopyalanir; pointer wordsq'ya konur, aktif havuza takas + eski free() loop()'ta.
  // Bos/gecersiz gövde -> 400 (varsayilana DONULMEZ). Guc yonetimine dokunmaz (uyandirmaz).
  auto *wHandler = new AsyncCallbackJsonWebHandler("/words", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonArray arr = json["w"].as<JsonArray>();
    if (arr.isNull()) { req->send(400, "application/json", "{\"err\":\"w[] yok\"}"); return; }

    // 1. gecis: gecerli string sayisi + blob boyutu (bos olmayan tekiller). Ust sinir 512.
    int n = 0; size_t blobLen = 0;
    for (JsonVariant v : arr) {
      const char *s = v.as<const char*>();
      if (!s || !*s) continue;
      if (n >= 512) break;
      n++; blobLen += strlen(s) + 1;
    }
    if (n == 0) { req->send(400, "application/json", "{\"err\":\"bos\"}"); return; }

    // heap: blob (yan yana '\0'-ayrik kelimeler) + idx (blob'a pointer'lar) + SpinPool.
    SpinPool *p = (SpinPool*)malloc(sizeof(SpinPool));
    char *blob  = (char*)malloc(blobLen);
    const char **idx = (const char**)malloc(sizeof(char*) * n);
    if (!p || !blob || !idx) { free(p); free(blob); free(idx);
      req->send(500, "application/json", "{\"err\":\"oom\"}"); return; }

    // 2. gecis: kopyala.
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

    // eski bekleyen (henuz loop takas etmemis) havuz varsa sizmasin: overwrite oncesi al & free.
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

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "yok"); });
  server.begin();
  Serial.println("[clawd] HTTP :80 ayakta (/e /status /words /health)");

  tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));  // fume letterbox
  setAnim(pickIdle());                               // acilista havuzdan rastgele bekleme maskotu

  // HUD: bantlari baslat + ilk cizim. sol-ust WiFi cubuklari; alt sol iki satir
  // (spinner + statusLine ozeti). statusLine POST /status ile dolar.
  hud.begin(&tft);
  hud.setWifi(true, WiFi.RSSI());
  hud.setAction("");                               // idle: sol-alt bos
  hud.render();

  minis.begin(&tft);                               // yan sutun mini-clawd filosu (bos baslar)
  usage.begin(&tft);                               // kota gorunumu (kose butonuyla acilir)
  g_ready = true;                                  // zemin+HUD+minis hazir -> loop artik cizebilir
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

// Dokunma var mi? Basinc yeterli (uyandirma) + ham x/y doldur.
// rawx: jest algilama (GORELI hareket, kalibrasyon gerekmez).
// rawy: rawx ile birlikte kose-buton tespiti (kaba kalibrasyonla ekran px'e cevrilir).
static bool readTouch(int &rawx, int &rawy) {
  if (!ts.tirqTouched() && !ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 200) return false;
  rawx = p.x;
  rawy = p.y;
  return true;
}

// Ham dokunus sag-ust kose (gorunum degistirici buton) bolgesinde mi?
// Kaba kalibrasyon yeter: bolge genis (UI_BTN_W x UI_BTN_H).
static bool inCorner(int rawx, int rawy) {
  int sx = map(rawx, RAW_X_MIN, RAW_X_MAX, 0, 319);
  int sy = map(rawy, RAW_Y_MIN, RAW_Y_MAX, 0, 239);
  sx = constrain(sx, 0, 319);
  sy = constrain(sy, 0, 239);
  return sx >= 320 - UI_BTN_W && sy <= UI_BTN_H;
}

// Gorunum degistir: normal <-> usage. Donuste zemin tazelenir, tum katmanlar
// kirlenir (temiz cizim); girase usage tam cizim ister. Anim/olay durumu KORUNUR.
static void toggleUsage() {
  uiUsage = !uiUsage;
  Serial.printf("[clawd] gorunum -> %s\n", uiUsage ? "usage" : "normal");
  uiToggleAnimate = true;                            // toggle topuzu kayarak gecsin (yalniz bu yolda)
  if (uiUsage) {
    usage.markAllDirty();                            // render() zemin dahil her seyi cizer
  } else {
    tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));
    hud.markAllDirty();
    minis.markAllDirty();
    lastFrame = 0;                                   // maskot bir sonraki tick'te hemen cizilsin
  }
}

// Dokunmatik jest durum makinesi — her loop cagirilir. DOWN/UP kenarlari + dokunma
// suresince ham x araligindan iki jesti ayirt eder:
//   TG_DOUBLETAP = iki kisa tap ard arda (gidiklama)
//   TG_STROKE    = tek dokunusta genis yatay hareket (surtme/oksama)
enum TouchGesture { TG_NONE = 0, TG_DOUBLETAP, TG_STROKE };
static TouchGesture touchGesture(bool isTouched, int rawx) {
  static bool     contact = false;             // debounce'lu temas suruyor mu
  static uint32_t startMs = 0, seenMs = 0, lastTapMs = 0;
  static int      minX = 0, maxX = 0;
  static bool     settled = false;
  TouchGesture g = TG_NONE;
  uint32_t now = millis();

  if (isTouched) {
    seenMs = now;
    if (!contact) {                            // yeni temas basliyor
      contact = true; startMs = now; minX = maxX = rawx; settled = false;
    } else if (now - startMs >= TOUCH_SETTLE_MS) {   // settle sonrasi x araligini izle
      if (!settled) { minX = maxX = rawx; settled = true; }   // ilk gurultuyu at, araligi sifirla
      if (rawx < minX) minX = rawx;
      if (rawx > maxX) maxX = rawx;
    }
  } else if (contact && now - seenMs >= TOUCH_RELEASE_MS) {   // GERCEK birakma (debounce doldu)
    contact = false;
    uint32_t dur = seenMs - startMs;           // temas suresi (debounce kuyrugu haric)
    int range = maxX - minX;
    if (range >= STROKE_MIN_RAW) {             // tek temasta genis hareket -> surtme/oksama
      g = TG_STROKE; lastTapMs = 0;
    } else if (dur <= TAP_MAX_MS) {            // kisa + sabit -> tap
      if (lastTapMs && now - lastTapMs <= DOUBLETAP_MS) { g = TG_DOUBLETAP; lastTapMs = 0; }
      else lastTapMs = now;                    // ilk tap; ikinciyi bekle
    }
    // uzun + hareketsiz (parmak dinleniyor) -> yok say
    if (g || range || dur)
      Serial.printf("[touch] dur=%lu range=%d -> %s\n", (unsigned long)dur, range,
                    g == TG_STROKE ? "STROKE(love)" : g == TG_DOUBLETAP ? "DOUBLETAP(tickle)" : "tap/none");
  }
  return g;
}

void loop() {
  // setup() basarisiz/erken bitmisse ( or. WiFi yok): fume zemin ve HUD hic kurulmadi.
  // Cizim yapma -> "WiFi BAGLANAMADI" hata ekrani bozulmadan kalsin (maskot fume-karesi
  // siyah zemine basilmasin). Kullanici WiFi'yi duzeltip (install.sh) cihazi resetler.
  if (!g_ready) { delay(200); return; }

  bool wasAsleep = power.asleep();
  int  rawx = 0, rawy = 0;
  bool isTouched = readTouch(rawx, rawy);

  // Sag-ust kose butonu: kosede BASLAYAN temas gorunumu degistirir ve jest
  // makinesine GIRMEZ (cift-tap toggle'i tickle olarak da sayilmasin). Uykudayken
  // dokunus once uyandirir (asagida notifyActivity), gorunum degismez.
  static bool     prevTouched  = false;
  static bool     cornerPress  = false;              // aktif temas kosede mi basladi
  static uint32_t lastToggleMs = 0;
  if (isTouched && !prevTouched) {                   // DOWN kenari
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
      setAnim(restAnim()); setHudCat(HC_IDLE);
    } else if (!agentMode) {
      int id = mapEvent(e);
      if (id == ANIM_IDLE) id = restAnim();          // "dinlenmeye don" hedefi: ctx kritikse beyin, degilse havuzdan maskot
      if (id >= 0 && id != (int)curAnim) setAnim((AnimId)id);
    }

    updateHud(e);                                      // HUD koseleri her olayda guncellenir
    Serial.printf("[clawd] olay k=%s g=%s\n", e.k, e.g);
  }

  // 2) dokunma = aktivite (uykuda da uyandirir)
  if (isTouched) power.notifyActivity();

  // 2b) dokunmatik jestleri: cift-dokunus -> gidiklama, surtme -> oksama (kalpler).
  // Yalniz UYANIK + agent modu DISINDA + NORMAL gorunumde tetiklenir (agent modu
  // buyuk pozu KILITLI tutar; usage ekraninda maskot yok). Uykudayken ilk
  // dokunus cihazi uyandirir; clawd bir sonraki etkilesime tepki verir.
  if (tg != TG_NONE && !power.asleep() && !agentMode && !uiUsage) {
    // Dokunus ONCESI pozu hatirla: tickle/love bitince idle'a DEGIL, o poza don
    // (orn. Claude dusunurken oksarsan -> love -> tekrar think). curAnim zaten
    // tickle/love ise (ust uste dokunus) uzerine yazma, gercek taban korunsun.
    if (curAnim != ANIM_TICKLE && curAnim != ANIM_LOVE) returnAnim = curAnim;
    if (tg == TG_DOUBLETAP)   setAnim(ANIM_TICKLE);
    else if (tg == TG_STROKE) setAnim(ANIM_LOVE);
  }

  // 3) guc durumunu guncelle + arka isik fade; durum degistiyse yan etki uygula
  if (power.tick()) {
    PowerManager::State st = power.state();
    applyPowerEdge(st);                              // CPU frekansi
    // Isik dustugunde clawd uyuklama pozuna gecer (kapali gozler + zzZZ).
    // (busy-gate mesgulken kismayi engeller -> normalde burada mini olmaz; yine de temizle.)
    if (st == PowerManager::DIM) { setAnim(ANIM_SLEEP); setHudCat(HC_IDLE); minis.clear(); agentActive = 0; agentMode = false; }
    // Uyandi: uyku pozundaysak idle'a don (bir olay yeni anim atadiysa ona dokunma).
    else if (st == PowerManager::ACTIVE && curAnim == ANIM_SLEEP) { setAnim(restAnim()); setHudCat(HC_IDLE); }
    // SLEEP: ekran kapali, cizim yok.
  }

  // uykudan yeni ciktiysak: zemini tazele + aktif gorunumu bastan ciz
  if (wasAsleep && !power.asleep()) {
    if (uiUsage) {
      usage.markAllDirty();                          // render() zemin dahil tam cizer
    } else {
      tft.fillScreen(tft.color565(BG_R, BG_G, BG_B));
      hud.markAllDirty();
      minis.markAllDirty();                          // temiz zemine yeniden cizsinler
    }
  }

  // 4) gecici ifade suresi doldu -> idle (flavor + tool adini da temizle)
  if (revertAt && millis() >= revertAt) {
    // Dokunmatik tickle/love -> dokunus oncesi poza don (think/hacking/idle...).
    // Diger gecici ifadeler (happy/oops/ask) eskisi gibi idle'a doner.
    bool touchAnim = (curAnim == ANIM_TICKLE || curAnim == ANIM_LOVE);
    AnimId back = touchAnim ? returnAnim : restAnim();
    setAnim(back);
    if (isIdlePose(back) || back == ANIM_BRAIN_FULL) setHudCat(HC_IDLE);
  }

  // 5) Status kuyrugu: Claude Code statusLine ozeti. GUC yonetimine DOKUNMAZ
  //    (ayri kuyruk, notifyActivity yok) -> status akisi cihazi uyanik tutmaz.
  //    Uykuda da tuketilir (buffer guncellenir); cizim yalniz uyanikken (asagida).
  StatusMsg sm;
  if (xQueueReceive(statusq, &sm, 0) == pdTRUE) {
    hud.setStatus(sm.m, sm.ctx, sm.h5, sm.wk);
    hud.setReset(sm.h5r, sm.wkr);
    usage.setStatus(sm.h5, sm.wk, sm.h5r, sm.wkr);   // kota ekrani ayni veriyle beslenir
    // ctx% esigi gecince, cihaz O ANDA "dinlenme" pozundaysa (idle/brain_full) aninda
    // gecis yap (bir sonraki olayi beklemeden) — guc yonetimine dokunmaz (bkz. yorum yukarida).
    bool nowHigh = sm.ctx >= CTX_BRAIN_THRESH;
    if (nowHigh != ctxHigh) {
      ctxHigh = nowHigh;
      if (!agentMode) {
        if (ctxHigh && isIdlePose(curAnim)) setAnim(ANIM_BRAIN_FULL);
        else if (!ctxHigh && curAnim == ANIM_BRAIN_FULL) setAnim(pickIdle());
      }
    }
  }

  // POST /words ile gelen yeni spinner havuzunu AKTIF ET (takas loop'ta -> kilitsiz guvenli).
  // g_spin'e yalniz bu task dokunur; eski heap havuz (varsa) burada free edilir.
  SpinPool *np;
  if (xQueueReceive(wordsq, &np, 0) == pdTRUE && np) {
    g_spin = np->idx; g_spinN = np->n;
    if (g_heapPool) { free(g_heapPool->blob); free((void*)g_heapPool->idx); free(g_heapPool); }
    g_heapPool = np;
    Serial.printf("[clawd] spinner havuzu guncellendi: %d kelime\n", np->n);
  }

  // 6) WiFi sinyalini periyodik yenile (her 4 sn). setWifi yalniz cubuk sayisi
  //    degisince yeniden cizer -> RSSI dalgalansa da bosuna SPI yok.
  static uint32_t lastWifiPoll = 0;
  if (millis() - lastWifiPoll >= 4000) {
    lastWifiPoll = millis();
    bool c = (WiFi.status() == WL_CONNECTED);
    hud.setWifi(c, c ? WiFi.RSSI() : -127);
  }

  // 6) cizim — uykuda cizme (ekran zaten kapali, CPU'yu bosa harcama).
  // Usage modunda maskot/HUD/mini yerine kota ekrani cizilir; anim durumu
  // arka planda ilerlemez ama korunur (donuste kaldigi pozdan surer).
  if (!power.asleep()) {
    if (uiUsage) {
      usage.render();                                // yalniz kirli parcalar cizilir
    } else {
      uint32_t now = millis();
      if (now - lastFrame >= ANIMS[curAnim].interval) {
        lastFrame = now;
        drawFrame(curAnim, frame);
        frame = (frame + 1) % ANIMS[curAnim].count;
      }
      hud.render();                                  // yalniz kirli koseler cizilir
      minis.tick();                                  // yan sutunlarda alt-agent mini'leri
    }
  }

  delay(power.asleep() ? 40 : 5);                   // uykuda daha uzun bekle (guc)
}
