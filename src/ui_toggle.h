#pragma once
// Sag-ust gorunum degistirici: iOS tarzi PILL TOGGLE (eski cift-yonlu takas oklarinin
// yerine). Kapali = normal maskot gorunumu (topuz solda, yatak sonuk gri),
// Acik = kota ekrani (topuz sagda, yatak clawd-turuncu).
//
// Iki ekran da AYNI cizimi kullanir (hud.h ve usage.h) -> ayni yerde, ayni buton
// hissi; tek fark "on" durumu. Dokunma bolgesi bundan cok daha genis
// (UI_BTN_W/UI_BTN_H, main.cpp'deki inCorner) — bu yalniz gorsel.
//
// Cizim: once kendi kutusu bg ile silinir (FreeFont'lar gibi arkaplan doldurmayan
// parcalar yan yana gelmesin), sonra yatak + topuz. Statik; yalniz durum
// degisince (mod gecisi / markAllDirty) yeniden cizilir.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// Toggle yerlesimi (ekran koordinatlari, 320x240 landscape).
constexpr int UI_TGL_W = 36, UI_TGL_H = 18;               // yatak olculeri
constexpr int UI_TGL_X = 320 - UI_TGL_W - 8, UI_TGL_Y = 4;
constexpr int UI_TGL_KR = 7;                              // topuz yaricapi

inline void drawUiToggle(TFT_eSPI *tft, uint16_t bg, bool on) {
  const int r = UI_TGL_H / 2;
  tft->fillRect(UI_TGL_X - 2, 0, UI_TGL_W + 6, UI_TGL_Y + UI_TGL_H + 4, bg);

  // yatak: acikken turuncu, kapaliyken sonuk gri (kart zemininden bir ton acik)
  const uint16_t track = on ? CLAWD_ORANGE : RGB565(72, 76, 84);
  tft->fillRoundRect(UI_TGL_X, UI_TGL_Y, UI_TGL_W, UI_TGL_H, r, track);

  // topuz: acikken sagda beyaz, kapaliyken solda kirik gri
  const int cy = UI_TGL_Y + r;
  const int cx = on ? UI_TGL_X + UI_TGL_W - r : UI_TGL_X + r;
  tft->fillCircle(cx, cy, UI_TGL_KR, on ? RGB565(255, 255, 255) : RGB565(168, 172, 180));
}
