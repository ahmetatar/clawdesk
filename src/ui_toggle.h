#pragma once
// Top-right view switcher: an iOS-style pill toggle. Off = mascot view (knob
// left, grey track), on = usage view (knob right, clawd-orange track).
//
// Both views (hud.h and usage.h) draw the same toggle in the same place. This is
// visual only — the touch zone is much larger (UI_BTN_W/UI_BTN_H, see inCorner
// in main.cpp).
//
// Drawn by clearing its own box first, then track + knob. Redrawn only when the
// state changes.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// Layout in screen coordinates (320x240 landscape).
constexpr int UI_TGL_W = 36, UI_TGL_H = 18;               // track size
constexpr int UI_TGL_X = 320 - UI_TGL_W - 8, UI_TGL_Y = 4;
constexpr int UI_TGL_KR = 7;                              // knob radius

// Set on a mode switch (main.cpp toggleUsage) and consumed by the next
// drawUiToggle call. Wake-ups and full redraws skip the slide.
inline bool uiToggleAnimate = false;

// Colors kept as 8-8-8: intermediate frames need lerp, which RGB565 cannot do.
constexpr uint8_t UI_TGL_OFF_TRACK[3] = {72, 76, 84},   UI_TGL_ON_TRACK[3] = {213, 82, 56};
constexpr uint8_t UI_TGL_OFF_KNOB[3]  = {168, 172, 180}, UI_TGL_ON_KNOB[3] = {255, 255, 255};

// One frame: t = 0 (off) .. 1 (on).
inline void drawUiToggleFrame(TFT_eSPI *tft, uint16_t bg, float t) {
  const int r = UI_TGL_H / 2;
  tft->fillRect(UI_TGL_X - 2, 0, UI_TGL_W + 6, UI_TGL_Y + UI_TGL_H + 4, bg);

  auto lerp = [t](const uint8_t a[3], const uint8_t b[3], int i) -> uint8_t {
    return (uint8_t)(a[i] + (b[i] - a[i]) * t);
  };
  const uint16_t track = RGB565(lerp(UI_TGL_OFF_TRACK, UI_TGL_ON_TRACK, 0),
                                lerp(UI_TGL_OFF_TRACK, UI_TGL_ON_TRACK, 1),
                                lerp(UI_TGL_OFF_TRACK, UI_TGL_ON_TRACK, 2));
  const uint16_t knob  = RGB565(lerp(UI_TGL_OFF_KNOB, UI_TGL_ON_KNOB, 0),
                                lerp(UI_TGL_OFF_KNOB, UI_TGL_ON_KNOB, 1),
                                lerp(UI_TGL_OFF_KNOB, UI_TGL_ON_KNOB, 2));
  tft->fillRoundRect(UI_TGL_X, UI_TGL_Y, UI_TGL_W, UI_TGL_H, r, track);

  const int x0 = UI_TGL_X + r, x1 = UI_TGL_X + UI_TGL_W - r;   // knob centre range
  tft->fillCircle((int)(x0 + (x1 - x0) * t + 0.5f), UI_TGL_Y + r, UI_TGL_KR, knob);
}

// Final off/on state. If uiToggleAnimate is set, plays a ~130 ms slide first
// (a short blocking loop; the 450 ms debounce covers it).
inline void drawUiToggle(TFT_eSPI *tft, uint16_t bg, bool on) {
  if (uiToggleAnimate) {
    uiToggleAnimate = false;
    constexpr int N = 8;
    for (int i = 1; i < N; i++) {
      float p = (float)i / N;
      float e = p * p * (3.0f - 2.0f * p);              // smoothstep
      drawUiToggleFrame(tft, bg, on ? e : 1.0f - e);
      delay(16);
    }
  }
  drawUiToggleFrame(tft, bg, on ? 1.0f : 0.0f);
}
