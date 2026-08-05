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

// Kayma animasyonu: mod gecisinde (main.cpp toggleUsage) TRUE'ya cekilir, ilk
// drawUiToggle cagrisi tuketir. Uykudan uyanma / tam yeniden cizim gibi
// yollarda animasyon YOK — toggle dogrudan son halinde cizilir.
inline bool uiToggleAnimate = false;

// Toggle renkleri 8-8-8 (ara kareler icin lerp gerekiyor; RGB565 karistirilamaz).
constexpr uint8_t UI_TGL_OFF_TRACK[3] = {72, 76, 84},   UI_TGL_ON_TRACK[3] = {213, 82, 56};
constexpr uint8_t UI_TGL_OFF_KNOB[3]  = {168, 172, 180}, UI_TGL_ON_KNOB[3] = {255, 255, 255};

// Tek kare: t = 0 (kapali) .. 1 (acik) arasi konum/renk.
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

  const int x0 = UI_TGL_X + r, x1 = UI_TGL_X + UI_TGL_W - r;   // topuz merkez araligi
  tft->fillCircle((int)(x0 + (x1 - x0) * t + 0.5f), UI_TGL_Y + r, UI_TGL_KR, knob);
}

// Kapali/acik son hal. uiToggleAnimate isaretliyse once ~130 ms'lik kayma
// oynatilir (bloklayan kisa dongu; toggle debounce'i 450 ms, jest makinesi bu
// sirada zaten kose basisini yok sayar).
inline void drawUiToggle(TFT_eSPI *tft, uint16_t bg, bool on) {
  if (uiToggleAnimate) {
    uiToggleAnimate = false;
    constexpr int N = 8;
    for (int i = 1; i < N; i++) {
      float p = (float)i / N;
      float e = p * p * (3.0f - 2.0f * p);              // smoothstep (yumusak giris/cikis)
      drawUiToggleFrame(tft, bg, on ? e : 1.0f - e);
      delay(16);
    }
  }
  drawUiToggleFrame(tft, bg, on ? 1.0f : 0.0f);
}
