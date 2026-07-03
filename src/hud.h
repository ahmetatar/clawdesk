#pragma once
// clawd HUD — clawd'in ALTINDAKI bosluga iki satir yazan hafif katman.
// clawd merkezde cizilir; anim'ler 58 satira KIRPILIR (ekran y[24,198)) -> alt
// serit y[198,240) HUD'a kalir, ust katmanla cakismaz (titreme yok).
//
// YERLESIM:
//   sol-ust    (y[0,24)): WiFi SINYAL CUBUKLARI (RSSI'den; bagli=yesil, kopuk=kirmizi).
//   sol-alt ust satir  (y~208): SPINNER/dusunme flavor metni ("Thinking...", "Crafting..."),
//                       clawd-turuncu.
//   sol-alt alt satir  (y~228): CLAUDE CODE STATUS LINE ozeti — "Model  ctx:%  5h:%  wk:%",
//                       yuzdeler kullanima gore RENKLI (yesil<50, sari<80, kirmizi>=80).
//   (Onceki sag-alt "tool adi" KALDIRILDI — iki satirlik alt bloga yer acmak icin.)
//
// Veri: statusLine -> POST /status (model+ctx+5h+wk); flavor -> setAction (olaylardan).
// CIZIM DISIPLINI: yalniz ilgili satir DEGISINCE yeniden cizilir (once bg temizle).

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

class Hud {
public:
  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = tft->color565(BG_R, BG_G, BG_B);
    _wifiDirty = _statusDirty = _actionDirty = true;
  }

  // rssi: WiFi.RSSI() (dBm, negatif). connected=false -> tek kirmizi cubuk.
  // Periyodik cagrilir; yalniz cubuk sayisi/durum DEGISINCE yeniden cizer.
  void setWifi(bool connected, int rssi) {
    int bars = !connected     ? 1
             : rssi >= -60     ? 4
             : rssi >= -68     ? 3
             : rssi >= -76     ? 2
                               : 1;
    if (connected == _connected && bars == _bars) return;   // degisiklik yok
    _connected = connected;
    _bars = bars;
    _wifiDirty = true;
  }

  // Claude Code status line ozeti (alt satir). model bos -> "Claude" varsayilani.
  // ctx/h5/wk: yuzde (0..100); <0 -> "etiket -" sonuk placeholder (veri bekleniyor).
  void setStatus(const char *model, int ctx, int h5, int wk) {
    strlcpy(_model, model ? model : "", sizeof(_model));
    _ctx = ctx; _h5 = h5; _wk = wk;
    _statusDirty = true;
  }

  // Spinner/dusunme flavor metni (ust satir). Bos -> temizlenir. Renk clawd-turuncu.
  void setAction(const char *txt) {
    strlcpy(_action, txt ? txt : "", sizeof(_action));
    _actionDirty = true;
  }

  void markAllDirty() { _wifiDirty = _statusDirty = _actionDirty = true; }

  // Her loop cagrilir; yalniz kirli satirlari cizer. Uyku sirasinda main CAGIRMAZ.
  void render() {
    if (!_tft) return;
    if (_wifiDirty)   { drawWifi();   _wifiDirty   = false; }
    if (_actionDirty) { drawAction(); _actionDirty = false; }
    if (_statusDirty) { drawStatus(); _statusDirty = false; }
  }

private:
  static constexpr int SPIN_Y   = 208;    // ust satir (spinner/flavor) dikey orta
  static constexpr int STATUS_Y = 228;    // alt satir (status line) dikey orta

  void clearRect(int x, int y, int w, int h) { _tft->fillRect(x, y, w, h, _bg); }

  // Sol-ust: 4 sinyal cubugu. Dolu cubuklar = _bars; bagli yesil, kopuk kirmizi.
  void drawWifi() {
    clearRect(0, 0, 40, 24);
    const int x0 = 5, base = 19, bw = 3, gap = 2;
    const int h[4] = {5, 9, 13, 17};
    uint16_t on  = _connected ? _tft->color565(70, 200, 110)
                              : _tft->color565(220, 70, 60);
    uint16_t off = _tft->color565(58, 62, 70);
    for (int i = 0; i < 4; i++) {
      int bx = x0 + i * (bw + gap);
      _tft->fillRect(bx, base - h[i], bw, h[i], (i < _bars) ? on : off);
    }
  }

  // Kullanima gore renk: <%50 yesil, <%80 sari, >=%80 kirmizi (statusLine ile ayni).
  uint16_t pctColor(int p) {
    return p >= 80 ? _tft->color565(220, 70, 60)
         : p >= 50 ? _tft->color565(230, 195, 60)
                   : _tft->color565(70, 200, 110);
  }

  // Tek "etiket %" segmenti ciz, yeni x dondur. pct<0 (veri yok) -> "etiket -"
  // sonuk placeholder olarak yine ciz (bar bos kalmasin); gercek deger gelince
  // renkli % ile dolar.
  int drawPct(int x, const char *label, int pct) {
    char buf[14];
    uint16_t col;
    if (pct < 0) {
      snprintf(buf, sizeof(buf), "%s -", label);          // "ctx: -" (veri bekleniyor)
      col = _tft->color565(120, 120, 132);                // sonuk gri placeholder
    } else {
      snprintf(buf, sizeof(buf), "%s %d%%", label, pct);  // "ctx: 12%" (renkli)
      col = pctColor(pct);
    }
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(col, _bg);
    _tft->drawString(buf, x, STATUS_Y, 2);
    return x + _tft->textWidth(buf, 2) + 8;
  }

  // Ust satir: spinner/dusunme flavor metni (clawd-turuncu).
  void drawAction() {
    clearRect(0, SPIN_Y - 10, 210, 20);
    if (!_action[0]) return;
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(CLAWD_ORANGE, _bg);
    _tft->drawString(_action, 6, SPIN_Y, 2);
  }

  // Alt satir: model (sonuk gri) + ctx/5h/wk (renkli). Soldan saga dizilir.
  void drawStatus() {
    clearRect(0, STATUS_Y - 11, 320, 23);
    int x = 6;
    const char *model = _model[0] ? _model : "Claude";   // veri yok -> varsayilan
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(_tft->color565(150, 150, 162), _bg);
    _tft->drawString(model, x, STATUS_Y, 2);
    x += _tft->textWidth(model, 2) + 10;
    x = drawPct(x, "ctx:", _ctx);
    x = drawPct(x, "5h:",  _h5);
    x = drawPct(x, "wk:",  _wk);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t _bg = 0;
  char     _model[12] = {0};
  int      _ctx = -1, _h5 = -1, _wk = -1;
  char     _action[32] = {0};
  bool     _connected  = false;
  int      _bars       = 1;
  bool     _wifiDirty = true, _statusDirty = true, _actionDirty = true;
};
