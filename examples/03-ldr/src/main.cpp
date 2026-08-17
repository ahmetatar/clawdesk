// clawd examples — 03 LDR (light sensor)
// Reads the CYD's on-board LDR from GPIO34 and prints the raw value plus a
// light/dark classification. Darkness lights the BLUE LED — clawd's "room went dark
// -> sleep mode" indicator.
//
// The LDR on THIS board is missing or faulty: GPIO34 does not respond to light and
// reads a constant ~2425 (only the internal 1M+1M divider remains). A known issue;
// the code is correct and works on a CYD with a healthy LDR. For an external sensor,
// wire it to GPIO35 (3.3V-LDR-G35-10k-GND) and set PIN_LDR=35.

#include <Arduino.h>

constexpr int PIN_LDR = 34;   // input-only ADC pin (use 35 for an external LDR)
constexpr int PIN_B   = 17;   // blue LED (active-low) — sleep indicator

// The ESP32 ADC is 12-bit, so 0..4095. With a healthy LDR, bright reads LOW and dark
// reads HIGH. Read your own values from the serial monitor and put the midpoint here.
constexpr int DARK_THRESHOLD = 2500;

static void blue(bool on) { digitalWrite(PIN_B, on ? LOW : HIGH); }  // active-low

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 03-ldr starting (GPIO34)");
  pinMode(PIN_B, OUTPUT);
  blue(false);
  analogReadResolution(12);
  Serial.printf("[clawd] DARK_THRESHOLD = %d\n", DARK_THRESHOLD);
}

void loop() {
  long sum = 0;
  const int N = 16;
  for (int i = 0; i < N; i++) { sum += analogRead(PIN_LDR); delay(2); }
  int val = sum / N;

  bool dark = val >= DARK_THRESHOLD;   // if the relationship is inverted: val <= DARK_THRESHOLD
  blue(dark);

  Serial.printf("[clawd] LDR=%4d  ->  %s\n", val, dark ? "DARK (sleep, blue)" : "light");
  delay(300);
}
