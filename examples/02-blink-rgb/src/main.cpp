// clawd examples — 02 Blink RGB
// Cycles the CYD's on-board RGB LED through single and mixed colours. No external
// hardware. The goal is to see the pin mapping and the active-low behaviour for
// yourself — the basis of clawd's mood light.
//
// Note for this board: the red channel (GPIO4) does not light; the sub-LED appears
// faulty. Green (16) and blue (17) are fine. See the README.

#include <Arduino.h>

// CYD on-board RGB LED pins (active-low)
constexpr int PIN_R = 4;
constexpr int PIN_G = 16;
constexpr int PIN_B = 17;

// active-low: true => lit (we write LOW)
static void setRGB(bool r, bool g, bool b) {
  digitalWrite(PIN_R, r ? LOW : HIGH);
  digitalWrite(PIN_G, g ? LOW : HIGH);
  digitalWrite(PIN_B, b ? LOW : HIGH);
}

static void show(const char *name, bool r, bool g, bool b, int ms) {
  Serial.printf("[clawd] LED -> %s\n", name);
  setRGB(r, g, b);
  delay(ms);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 02-blink-rgb starting (on-board RGB LED)");

  pinMode(PIN_R, OUTPUT);
  pinMode(PIN_G, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  setRGB(false, false, false);  // all off
}

void loop() {
  // 1) single colours — verify which pin is which
  show("RED",   true,  false, false, 600);
  show("GREEN", false, true,  false, 600);
  show("BLUE",  false, false, true,  600);

  // 2) mixes — are all three channels working
  show("YELLOW (R+G)",  true,  true,  false, 600);
  show("CYAN (G+B)",    false, true,  true,  600);
  show("MAGENTA (R+B)", true,  false, true,  600);
  show("WHITE (R+G+B)", true,  true,  true,  600);

  // 3) a short blackout, so the cycle is distinguishable
  show("OFF", false, false, false, 800);
}
