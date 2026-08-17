// clawd examples — 07 Async events + permission
// The body of the clawd protocol on ESPAsyncWebServer:
//   POST /e         -> fire-and-forget event (204); the device reacts with state/LED
//   POST /perm      -> permission question; returns {pending:true} and opens a prompt
//   GET  /perm/{id} -> poll for the decision: {decision:allow|deny} | {pending:true}
//   GET  /health    -> liveness
//
// Critical: AsyncWebServer callbacks run on a SEPARATE task and must never touch the
// display (SPI). So the /e callback pushes onto a thread-safe queue and all drawing
// happens in loop(); the /perm callback writes a mutex-protected slot, and loop()
// draws the prompt while touch resolves it.

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
#include "secrets.h"

// --- pins ---
constexpr int T_CLK = 25, T_CS = 33, T_MOSI = 32, T_MISO = 39;  // touch (HSPI)
constexpr int LED_G = 16, LED_B = 17;                            // green/blue (red is dead)

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(T_CS);          // polling, for the permission decision
AsyncWebServer server(80);

// Touch calibration from 04; only needs to be accurate enough to tell top from bottom.
int RAW_Y_MIN = 1600, RAW_Y_MAX = 3300;

// --- event queue ---
struct Ev { char k[24]; char g[10]; char s[48]; bool ok; bool on; int ctx; };
QueueHandle_t evq;

// --- permission slot (mutex-protected) ---
enum PState { P_NONE, P_PENDING, P_ALLOW, P_DENY };
struct Perm { int id; PState state; bool shown; char tool[24]; char s[48]; char risk[8]; uint32_t decidedAt; };
Perm gperm = {0, P_NONE, false, "", "", "", 0};   // guarded by the mutex, so no volatile
SemaphoreHandle_t permMux;

static void led(bool g, bool b) {           // active-low
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// ---------- screen: event state ----------
static void drawState(const char *label, const char *detail, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("clawd", tft.width() / 2, 6, 2);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(label, tft.width() / 2, 130, 4);
  if (detail && detail[0]) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(detail, tft.width() / 2, 175, 2);
  }
}

// event -> clawd state (the mapping in section 9 of the protocol)
static void renderEvent(const Ev &e) {
  const char *k = e.k;
  if (!strcmp(k, "think"))            { e.on ? drawState("THINKING", "", TFT_BLUE)  : drawState("clawd", "ready", TFT_DARKGREY);
                                        led(false, e.on); }
  else if (!strcmp(k, "tool.pre")) {
    if (!strcmp(e.g, "exec"))         drawState("HACKING", e.s, TFT_GREEN);
    else if (!strcmp(e.g, "edit"))    drawState("WRITING", e.s, TFT_GREEN);
    else if (!strcmp(e.g, "search") || !strcmp(e.g, "read")) drawState("SEARCHING", e.s, TFT_CYAN);
    else if (!strcmp(e.g, "web"))     drawState("SURFING", e.s, TFT_CYAN);
    else                              drawState("WORKING", e.s, TFT_GREEN);
    led(true, false);
  }
  else if (!strcmp(k, "tool.post"))   { e.ok ? drawState("OK", "", TFT_GREEN) : drawState("OOPS", "error", TFT_BLUE); led(e.ok, !e.ok); }
  else if (!strcmp(k, "git"))         { drawState("GIT!", e.s, TFT_GREEN); led(true, false); }
  else if (!strcmp(k, "compact"))     { drawState("DEFRAG", "memory", TFT_BLUE); led(false, true); }
  else if (!strcmp(k, "wait"))        { drawState("WAITING", "", TFT_CYAN); led(false, true); }
  else if (!strcmp(k, "agent.spawn")) { drawState("CLONE", e.s, TFT_GREEN); led(true, false); }
  else if (!strcmp(k, "prompt.submit")){ drawState("PROMPT", e.s, TFT_CYAN); led(false, true); }
  else if (!strcmp(k, "session.start")){ drawState("HELLO", e.s, TFT_GREEN); led(true, false); }
  else if (!strcmp(k, "status"))      { char b[16]; snprintf(b, sizeof(b), "ctx %%%d", e.ctx); drawState("clawd", b, TFT_DARKGREY); led(false, false); }
  else                                { drawState(k, e.s, TFT_WHITE); led(false, false); }
  Serial.printf("[clawd] event k=%s g=%s -> screen\n", e.k, e.g);
}

// ---------- screen: permission prompt ----------
static void drawPerm(const Perm &p) {
  uint16_t risk = !strcmp(p.risk, "high") ? TFT_RED
                : !strcmp(p.risk, "med") ? TFT_ORANGE : TFT_GREEN;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("PERMIT?", tft.width() / 2, 12, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(p.tool, tft.width() / 2, 58, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(p.s, tft.width() / 2, 95, 2);
  tft.setTextColor(risk, TFT_BLACK);
  tft.drawString(p.risk, tft.width() / 2, 120, 2);
  // touch zones
  tft.drawRect(0, 150, tft.width(), 80, TFT_GREEN);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("TOP HALF = ALLOW", tft.width() / 2, 165, 2);
  tft.drawRect(0, 235, tft.width(), 80, TFT_RED);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("BOTTOM HALF = DENY", tft.width() / 2, 285, 2);
}

// ---------- WiFi ----------
static bool connectWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("connecting to WiFi...", tft.width() / 2, 140, 2);
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
  Serial.println("\n[clawd] 07-async-events starting");

  pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  led(false, false);

  tft.init();
  tft.setRotation(0);
  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);

  evq = xQueueCreate(16, sizeof(Ev));
  permMux = xSemaphoreCreateMutex();

  if (!connectWiFi()) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WiFi FAILED", tft.width() / 2, 140, 2);
    Serial.println("[clawd] WiFi connection failed");
    return;
  }
  IPAddress ip = WiFi.localIP();
  Serial.printf("[clawd] WiFi OK. IP: %s\n", ip.toString().c_str());
  if (MDNS.begin("clawd")) MDNS.addService("http", "tcp", 80);

  // --- routes ---
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"fw\":\"0.2.0\",\"caps\":[\"led\",\"touch\"]}");
  });

  // POST /e (JSON body) -> push onto the queue, 204
  auto *eHandler = new AsyncCallbackJsonWebHandler("/e", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonObject o = json.as<JsonObject>();
    Ev e{};
    strlcpy(e.k, o["k"] | "?", sizeof(e.k));
    JsonObject d = o["d"];
    strlcpy(e.g, d["g"] | "", sizeof(e.g));
    strlcpy(e.s, d["s"] | (d["tool"] | (d["op"] | "")), sizeof(e.s));
    e.ok = d["ok"] | true;
    e.on = d["on"] | false;
    e.ctx = d["ctx"] | -1;
    xQueueSend(evq, &e, 0);
    req->send(204);
  });
  eHandler->setMethod(HTTP_POST);          // POST only, so it cannot capture other methods
  server.addHandler(eHandler);

  // POST /perm (JSON body) -> write the slot, {pending:true}
  auto *permHandler = new AsyncCallbackJsonWebHandler("/perm", [](AsyncWebServerRequest *req, JsonVariant &json) {
    JsonObject o = json.as<JsonObject>();
    JsonObject d = o["d"];
    xSemaphoreTake(permMux, portMAX_DELAY);
    gperm.id = o["id"] | 0;
    gperm.state = P_PENDING;
    gperm.shown = false;
    strlcpy(gperm.tool, d["tool"] | "?", sizeof(gperm.tool));
    strlcpy(gperm.s, d["s"] | "", sizeof(gperm.s));
    strlcpy(gperm.risk, d["risk"] | "low", sizeof(gperm.risk));
    xSemaphoreGive(permMux);
    Serial.printf("[clawd] /perm id=%d tool=%s risk=%s\n", gperm.id, gperm.tool, gperm.risk);
    req->send(200, "application/json", "{\"pending\":true}");
  });
  permHandler->setMethod(HTTP_POST);       // critical: otherwise GET /perm/{id} hits this handler and corrupts the state
  server.addHandler(permHandler);

  // GET /perm/{id} -> poll for the decision
  server.on("^\\/perm\\/([0-9]+)$", HTTP_GET, [](AsyncWebServerRequest *req) {
    int id = req->pathArg(0).toInt();
    xSemaphoreTake(permMux, portMAX_DELAY);
    PState st = (gperm.id == id) ? gperm.state : P_NONE;
    xSemaphoreGive(permMux);
    if (st == P_ALLOW)      req->send(200, "application/json", "{\"decision\":\"allow\"}");
    else if (st == P_DENY)  req->send(200, "application/json", "{\"decision\":\"deny\"}");
    else                    req->send(200, "application/json", "{\"pending\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[clawd] HTTP :80 up (/e /perm /perm/{id} /health)");

  drawState("clawd", "online, ready", TFT_GREEN);
}

// The permission decision: read the top/bottom half from the touchscreen.
static void resolvePermByTouch() {
  TS_Point p = ts.getPoint();
  if (p.z < 200) return;
  int sy = map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, tft.height() - 1);
  bool allow = sy < tft.height() / 2;     // top half = allow
  xSemaphoreTake(permMux, portMAX_DELAY);
  gperm.state = allow ? P_ALLOW : P_DENY;
  gperm.decidedAt = millis();
  xSemaphoreGive(permMux);
  if (allow) { drawState("ALLOW", "run it", TFT_GREEN); led(true, false); }
  else       { drawState("DENY", "cancelled", TFT_RED);  led(false, true); }
  Serial.printf("[clawd] permission decision: %s\n", allow ? "allow" : "deny");
  delay(1200);
  led(false, false);
  drawState("clawd", "online, ready", TFT_DARKGREY);
}

void loop() {
  // 1) drain the event queue (drawing happens here)
  Ev e;
  while (xQueueReceive(evq, &e, 0) == pdTRUE) renderEvent(e);

  // 2) permission: if pending, draw the prompt and resolve it from touch
  xSemaphoreTake(permMux, portMAX_DELAY);
  PState st = gperm.state;
  bool shown = gperm.shown;
  Perm snap = gperm;
  if (st == P_PENDING && !shown) gperm.shown = true;
  xSemaphoreGive(permMux);

  if (st == P_PENDING) {
    if (!shown) drawPerm(snap);
    resolvePermByTouch();
  }

  delay(10);
}
