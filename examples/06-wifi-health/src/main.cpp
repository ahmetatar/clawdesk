// clawd examples — 06 WiFi + mDNS + /health
// Joins WiFi, advertises itself as clawd.local, and answers GET /health with JSON.
// Shows the connection state and IP on screen. Uses the built-in WebServer, no
// external library. The first step of the clawd protocol's transport layer.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <TFT_eSPI.h>
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

// The capabilities that actually work on this board: green/blue LED + touch.
// (Audio needs an external speaker and the LDR is missing on this unit.)
static const char *HEALTH_JSON = "{\"fw\":\"0.1.0\",\"caps\":[\"led\",\"touch\"]}";

static void line(const char *s, int y, uint16_t color) {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(s, tft.width() / 2, y, 2);
}

static void handleHealth() {
  server.send(200, "application/json", HEALTH_JSON);
  Serial.println("[clawd] GET /health -> 200");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 06-wifi-health starting");

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  line("clawd", 10, TFT_CYAN);
  line("connecting to WiFi...", 40, TFT_WHITE);
  line(WIFI_SSID, 60, TFT_DARKGREY);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[clawd] connecting to WiFi: %s\n", WIFI_SSID);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[clawd] WiFi connection FAILED (SSID/password? 2.4GHz?)");
    tft.fillScreen(TFT_BLACK);
    line("WiFi FAILED", 100, TFT_RED);
    line("SSID/password? 2.4GHz?", 130, TFT_DARKGREY);
    return;
  }

  IPAddress ip = WiFi.localIP();
  Serial.printf("[clawd] WiFi OK. IP: %s\n", ip.toString().c_str());

  // mDNS: clawd.local
  if (MDNS.begin("clawd")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[clawd] mDNS: http://clawd.local/");
  } else {
    Serial.println("[clawd] mDNS could not start");
  }

  server.on("/health", HTTP_GET, handleHealth);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.println("[clawd] HTTP server :80 up");

  // show the result
  tft.fillScreen(TFT_BLACK);
  line("clawd ONLINE", 20, TFT_GREEN);
  line("IP:", 70, TFT_DARKGREY);
  line(ip.toString().c_str(), 92, TFT_WHITE);
  line("http://clawd.local/health", 150, TFT_CYAN);
}

void loop() {
  server.handleClient();
}
