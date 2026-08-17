// clawd examples — 05 Touch IRQ (touch-to-wake)
// "Sleep when idle, wake on touch" via the IRQ pin — clawd's real idle/wake behavior.
//
// A touch pulls the XPT2046's IRQ line low, and the library sets its wake flag on
// that falling edge. That makes IRQ ideal for catching a tap (waking up); for
// continuous finger tracking, polling is better — see 04.
//
// While awake the device shows touches; after SLEEP_MS with none it goes to sleep
// (screen dark, Zzz) and wakes instantly on the next touch.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

constexpr int T_CLK  = 25;
constexpr int T_CS   = 33;
constexpr int T_MOSI = 32;
constexpr int T_MISO = 39;
constexpr int T_IRQ  = 36;     // the difference from 04: the IRQ pin is supplied
constexpr int PIN_B  = 17;     // blue LED (active-low)

constexpr uint32_t SLEEP_MS = 5000;   // sleep after this long without a touch

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);   // constructed with IRQ

bool sleeping = false;
uint32_t lastTouch = 0;

static void drawAwake() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("AWAKE", tft.width() / 2, tft.height() / 2 - 10, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("5 s untouched -> sleep", tft.width() / 2, tft.height() / 2 + 30, 2);
}

static void drawSleep() {
  tft.fillScreen(TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_NAVY);
  tft.drawString("Zzz...", tft.width() / 2, tft.height() / 2 - 10, 4);
  tft.setTextColor(TFT_DARKGREY, TFT_NAVY);
  tft.drawString("touch -> wake", tft.width() / 2, tft.height() / 2 + 30, 2);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 05-touch-irq (touch-to-wake, HSPI)");

  pinMode(PIN_B, OUTPUT);
  digitalWrite(PIN_B, HIGH);

  tft.init();
  tft.setRotation(0);

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);

  drawAwake();
  lastTouch = millis();
  Serial.println("[clawd] awake. 5 s untouched -> sleep. Touch (IRQ) to wake.");
}

void loop() {
  bool touched = ts.touched();      // IRQ-gated: true on the falling edge

  if (touched) {
    TS_Point p = ts.getPoint();
    digitalWrite(PIN_B, LOW);
    lastTouch = millis();

    if (sleeping) {
      sleeping = false;
      drawAwake();
      Serial.printf("[clawd] TOUCH -> AWAKE!  (z=%d)\n", p.z);
    } else {
      tft.fillCircle(tft.width() / 2, tft.height() - 30, 5, TFT_GREEN);
      Serial.printf("[clawd] touch while awake z=%d x=%d y=%d\n", p.z, p.x, p.y);
    }
    delay(60);
    digitalWrite(PIN_B, HIGH);
  }

  // go to sleep when idle
  if (!sleeping && millis() - lastTouch > SLEEP_MS) {
    sleeping = true;
    drawSleep();
    Serial.println("[clawd] idle -> sleep (Zzz). A touch wakes it.");
  }

  delay(10);
}
