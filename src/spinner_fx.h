#pragma once
// The device's take on the Claude Code CLI spinner line:
//
//     [*]  Cogitating  . . .
//      ^      ^         ^
//      |      |         +-- 3 dots, brightening left to right in a wave
//      |      +------------ spinner word (from CC's real gerund pool)
//      +------------------- Claude asterisk mark, softly pulsing
//
// No flicker, by design: the word is drawn only when the text changes, and the
// mark and dots own fixed pixels that the animation merely recolors — nothing is
// ever cleared behind them. Brightness is quantized to 16 steps and only written
// to SPI when the step changes, and the dimmest step still blends 30% toward
// clawd-orange so nothing ever reads as a blink.
//
// Usage: begin() once; set()+draw() when the text changes; tick() every loop.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>
#include "config.h"

// Claude asterisk mark, 9x9 hand-drawn: full vertical/horizontal rays plus 4
// diagonals. Kept small to match the ~11px body height of the Font2 text.
static const int MARK_N = 9;
static const char *const CLAUDE_MARK[MARK_N] = {
  "....#....",
  "....#....",
  ".#..#..#.",
  "..#.#.#..",
  "#########",
  "..#.#.#..",
  ".#..#..#.",
  "....#....",
  "....#....",
};

class SpinnerFx {
public:
  static constexpr int ICON_W   = MARK_N;   // mark width/height
  static constexpr int ICON_GAP = 6;        // mark to word
  static constexpr int DOT_GAP  = 5;    // word to first dot
  static constexpr int DOT_STEP = 5;    // dot spacing
  static constexpr int DOT_SZ   = 2;    // dot size (px)
  static constexpr int DOTS_W   = DOT_SZ + 2 * DOT_STEP;

  void begin(TFT_eSPI *tft, uint16_t bg) { _tft = tft; _bg = bg; _on = false; }

  // txt : the text to show ("Cogitating" or "Shipping!")
  // anim: true  -> spinner (mark + animated dots)
  //       false -> plain static flavor text (neither).
  void set(const char *txt, bool anim) {
    strlcpy(_txt, txt ? txt : "", sizeof(_txt));
    _anim = anim;
  }

  // Total width, so the caller can size the band it clears.
  int width() {
    if (!_txt[0]) return 0;
    _tft->setTextFont(2);
    int tw = _tft->textWidth(_txt, 2);
    return _anim ? (ICON_W + ICON_GAP + tw + DOT_GAP + DOTS_W) : tw;
  }

  // Draw the line once, after the caller has cleared the background.
  // centered=false -> x is the left edge; true -> x is the centre.
  void draw(int x, int cy, bool centered) {
    _on = false;
    if (!_tft || !_txt[0]) return;
    _tft->setTextFont(2);
    int tw    = _tft->textWidth(_txt, 2);
    int total = width();
    int left  = centered ? x - total / 2 : x;
    int tx    = left + (_anim ? ICON_W + ICON_GAP : 0);

    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(CLAWD_ORANGE, _bg);
    _tft->drawString(_txt, tx, cy, 2);
    if (!_anim) return;

    _iconX = left;
    _iconY = cy - ICON_W / 2;
    _dotX  = tx + tw + DOT_GAP;
    _dotY  = cy + 4;                       // baseline
    _iconStep = _dotStep[0] = _dotStep[1] = _dotStep[2] = -1;
    _on = true;
    tick(true);                            // draw the first frame immediately
  }

  // Called every loop. Only recolors parts whose brightness step changed.
  void tick(bool force = false) {
    if (!_on) return;
    uint32_t ms = millis();

    int s = level(ms, PULSE_MS, 0.0f);
    if (force || s != _iconStep) { _iconStep = s; drawIcon(shade(s)); }

    for (int i = 0; i < 3; i++) {
      // Phase offset makes the brightness wave travel left to right.
      int d = level(ms, DOT_MS, -0.18f * i);
      if (force || d != _dotStep[i]) { _dotStep[i] = d; drawDot(i, shade(d)); }
    }
  }

  // The line is hidden or overdrawn: make tick() a no-op.
  void stop() { _on = false; }

private:
  static constexpr uint32_t PULSE_MS = 1500;   // mark pulse period
  static constexpr uint32_t DOT_MS   = 1100;   // dot wave period
  static constexpr int      STEPS    = 16;     // brightness steps
  static constexpr float    FLOOR    = 0.30f;  // dimmest step (0 would read as a blink)

  // ms -> brightness step 0..STEPS-1 (cosine, so it eases in and out).
  static int level(uint32_t ms, uint32_t period, float shift) {
    float p = (float)(ms % period) / (float)period + shift;
    p -= floorf(p);
    float k = 0.5f - 0.5f * cosf(p * 6.28318531f);
    return (int)(k * (STEPS - 1) + 0.5f);
  }

  // Step -> a blend between the background and clawd-orange.
  uint16_t shade(int s) {
    float t = FLOOR + (1.0f - FLOOR) * (float)s / (float)(STEPS - 1);
    uint8_t r = (uint8_t)(BG_R + (213 - BG_R) * t);
    uint8_t g = (uint8_t)(BG_G + (82  - BG_G) * t);
    uint8_t b = (uint8_t)(BG_B + (56  - BG_B) * t);
    return RGB565(r, g, b);
  }

  // Repaints only the mark's set pixels, coalescing runs into single HLines.
  void drawIcon(uint16_t col) {
    for (int y = 0; y < MARK_N; y++) {
      const char *row = CLAUDE_MARK[y];
      int x = 0;
      while (x < MARK_N) {
        if (row[x] != '#') { x++; continue; }
        int run = 0;
        while (x + run < MARK_N && row[x + run] == '#') run++;
        _tft->drawFastHLine(_iconX + x, _iconY + y, run, col);
        x += run;
      }
    }
  }

  void drawDot(int i, uint16_t col) {
    _tft->fillRect(_dotX + i * DOT_STEP, _dotY, DOT_SZ, DOT_SZ, col);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t  _bg  = 0;
  char      _txt[40] = {0};
  bool      _anim = false, _on = false;
  int       _iconX = 0, _iconY = 0, _dotX = 0, _dotY = 0;
  int       _iconStep = -1, _dotStep[3] = {-1, -1, -1};
};
