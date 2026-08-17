// clawd examples — 01 Hello World
// CYD / ESP32-2432S028R: draw text on the display.
// Goal: does the board program, and are the TFT pins right? Clearing this clears
// the hardest trap.

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();  // pins come from build_flags in platformio.ini

// Draw one centred line
static void drawCentered(const char *text, int y, uint16_t color, uint8_t font) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(text, tft.width() / 2, y, font);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[clawd] 01-hello-world starting...");

  tft.init();
  tft.setRotation(0);  // try 0/1/2/3 if the screen is sideways or upside down
  tft.fillScreen(TFT_BLACK);

  Serial.printf("[clawd] display: %d x %d\n", tft.width(), tft.height());

  // main message
  drawCentered("Hello, clawd!", 120, TFT_GREEN, 4);
  drawCentered("CYD display works", 160, TFT_WHITE, 2);
  drawCentered("ESP32-2432S028R", 185, TFT_DARKGREY, 2);

  Serial.println("[clawd] text drawn, setup done.");
}

void loop() {
  // Proof the display is really being driven: a dot pulsing in the corner.
  static bool on = false;
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 500) {
    last = now;
    on = !on;
    tft.fillCircle(tft.width() - 12, 12, 5, on ? TFT_RED : TFT_BLACK);
    Serial.printf("[clawd] heartbeat %s\n", on ? "on" : "off");
  }
}
