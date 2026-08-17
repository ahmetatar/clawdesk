// clawd examples — 04 Touch (XPT2046 + calibration)  [polling, working reference]
// Draws a dot where you press and prints the raw (x,y,z) values to the serial port.
// The blue LED lights while touching, as instant feedback.
//
// Three points verified on the CYD:
//  1) Touch must be on a SEPARATE bus: TFT_eSPI uses VSPI, so touch goes on HSPI.
//  2) SPIClass::begin() guards with 'if(_spi) return', so give it the right pins
//     BEFORE calling ts.begin().
//  3) Construct WITHOUT the IRQ pin so the library polls on every call.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// CYD touch pins (separate from the display)
constexpr int T_CLK  = 25;
constexpr int T_CS   = 33;
constexpr int T_MOSI = 32;
constexpr int T_MISO = 39;
constexpr int PIN_B  = 17;   // blue LED (active-low)

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(HSPI);          // separate bus (the TFT is on VSPI)
XPT2046_Touchscreen ts(T_CS);     // no IRQ -> polling

// Calibration: press the four corners and put the raw min/max here.
int RAW_X_MIN = 1800, RAW_X_MAX = 3300;
int RAW_Y_MIN = 1600, RAW_Y_MAX = 3300;

static void header() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("clawd touch", tft.width() / 2, 6, 2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("touch: dot + raw values on serial", tft.width() / 2, 26, 1);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 04-touch (polling, HSPI)");

  pinMode(PIN_B, OUTPUT);
  digitalWrite(PIN_B, HIGH);

  tft.init();
  tft.setRotation(0);
  header();

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);  // correct pins first
  ts.begin(touchSPI);
  ts.setRotation(0);

  Serial.println("[clawd] press the four corners and record the raw min/max");
}

void loop() {
  TS_Point p = ts.getPoint();
  if (p.z >= 200) {                       // a real touch
    digitalWrite(PIN_B, LOW);

    int sx = map(p.x, RAW_X_MIN, RAW_X_MAX, 0, tft.width()  - 1);
    int sy = map(p.y, RAW_Y_MIN, RAW_Y_MAX, 0, tft.height() - 1);
    sx = constrain(sx, 0, tft.width()  - 1);
    sy = constrain(sy, 0, tft.height() - 1);

    tft.fillCircle(sx, sy, 4, TFT_GREEN);
    Serial.printf("[clawd] raw x=%4d y=%4d z=%4d  ->  screen (%3d,%3d)\n",
                  p.x, p.y, p.z, sx, sy);
    delay(20);
  } else {
    digitalWrite(PIN_B, HIGH);
  }
}
