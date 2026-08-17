// clawd examples — 09 clawd animation (PixelLab idle, landscape)
// A 16-frame 64x64 idle animation from the PixelLab API, played in landscape.
// Each frame is scaled up and centred using a line buffer plus pushImage, which is
// fast and flicker-free.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "clawd_idle.h"

TFT_eSPI tft = TFT_eSPI();

const int S = 3;                                   // 64*3 = 192
const int SW = CLAWD_IDLE_W * S;                   // 192
static uint16_t linebuf[CLAWD_IDLE_W * S];

int xoff, yoff, frame = 0;
uint32_t last = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[clawd] 09 anim: %d frame %dx%d\n", CLAWD_IDLE_FRAMES, CLAWD_IDLE_W, CLAWD_IDLE_H);
  tft.init();
  tft.setRotation(1);                              // landscape 320x240
  tft.setSwapBytes(true);
  tft.fillScreen(tft.color565(239, 239, 234));     // cream
  xoff = (tft.width()  - SW) / 2;
  yoff = (tft.height() - CLAWD_IDLE_H * S) / 2;
}

static void drawFrame(int f) {
  const uint16_t *fr = clawd_idle[f];
  for (int y = 0; y < CLAWD_IDLE_H; y++) {
    // widen the source row S times horizontally
    for (int x = 0; x < CLAWD_IDLE_W; x++) {
      uint16_t c = fr[y * CLAWD_IDLE_W + x];
      for (int s = 0; s < S; s++) linebuf[x * S + s] = c;
    }
    // push the same row S times vertically
    for (int s = 0; s < S; s++) tft.pushImage(xoff, yoff + y * S + s, SW, 1, linebuf);
  }
}

void loop() {
  uint32_t now = millis();
  if (now - last >= 80) {                          // ~12 fps
    last = now;
    drawFrame(frame);
    frame = (frame + 1) % CLAWD_IDLE_FRAMES;
  }
}
