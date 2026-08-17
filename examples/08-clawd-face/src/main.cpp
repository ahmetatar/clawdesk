// clawd examples — 08 clawd (the real reference logo as an RGB565 bitmap, landscape)
// The clawd artwork converted to 267x240 RGB565 and pushed directly — no sprites or
// downsampling, anti-aliasing included. The display is in landscape (rotation 1).

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "clawd_img.h"

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[clawd] 08 clawd (real logo, RGB565, landscape)");

  tft.init();
  tft.setRotation(1);                 // landscape 320x240
  tft.fillScreen(tft.color565(239, 239, 234));   // cream background
  tft.setSwapBytes(true);             // byte order for the RGB565 array

  int x = (tft.width()  - CLAWD_IMG_W) / 2;       // centre it
  int y = (tft.height() - CLAWD_IMG_H) / 2;
  tft.pushImage(x, y, CLAWD_IMG_W, CLAWD_IMG_H, clawd_img);
  Serial.println("[clawd] image drawn");
}

void loop() {}
