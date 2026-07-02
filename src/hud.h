#pragma once
// clawd HUD — clawd'in etrafindaki BOS kenar bantlarina durum bilgisi yazan
// hafif katman. clawd merkezde 192x192 cizilir (y[24,216)); HUD YALNIZ ust bant
// (y[0,24)) ve alt bant (y[216,240)) icine yazar -> clawd'a ASLA dokunmaz.
//
// KOSE YERLESIMI:
//   ust bant : CLAUDE CODE STATUS LINE ozeti (model + ctx% + 5h% + wk%),
//              yuzdeler kullanima gore RENKLI (yesil<50, sari<80, kirmizi>=80) —
//              PC'deki statusLine ile birebir. Veri POST /status ile gelir.
//   sol-alt  : GUNCEL AKSIYON — kisa flavor metni ("Crafting..."), clawd-turuncu
//   sag-alt  : CALISAN TOOL ADI (Bash / Grep / Glob / Edit ...), sonuk gri
//
// CIZIM DISIPLINI: animasyon her frame merkezi tazeler; HUD ise SADECE ilgili
// bantin verisi degistiginde yeniden cizer. Bant = once fume bg ile temizlenir.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

class Hud {
public:
  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = tft->color565(BG_R, BG_G, BG_B);
    _statusDirty = _actionDirty = _toolDirty = true;
  }

  // ---- setters: ilgili banti kirli isaretle ----
  // Claude Code status line ozeti. model bos -> ust bant temizlenir.
  // ctx/h5/wk: yuzde (0..100); <0 -> o segment gizlenir (veri yok).
  void setStatus(const char *model, int ctx, int h5, int wk) {
    strlcpy(_model, model ? model : "", sizeof(_model));
    _ctx = ctx; _h5 = h5; _wk = wk;
    _statusDirty = true;
  }

  // Kisa aksiyon metni (flavor). Bos -> sol-alt temizlenir. Renk sabit clawd-turuncu.
  void setAction(const char *txt) {
    strlcpy(_action, txt ? txt : "", sizeof(_action));
    _actionDirty = true;
  }

  // Calisan tool adi (Bash/Grep/...). Bos -> sag-alt temizlenir.
  void setTool(const char *txt) {
    strlcpy(_tool, txt ? txt : "", sizeof(_tool));
    _toolDirty = true;
  }

  void markAllDirty() { _statusDirty = _actionDirty = _toolDirty = true; }

  // Her loop cagrilir; yalniz kirli bantlari cizer. Uyku sirasinda main CAGIRMAZ.
  void render() {
    if (!_tft) return;
    if (_statusDirty) { drawStatus(); _statusDirty = false; }
    if (_actionDirty) { drawAction(); _actionDirty = false; }
    if (_toolDirty)   { drawTool();   _toolDirty   = false; }
  }

private:
  static constexpr int BANDMID_T = 12;    // ust bant dikey orta (0..24)
  static constexpr int BANDMID_B = 228;   // alt bant dikey orta (216..240)

  void clearRect(int x, int y, int w, int h) { _tft->fillRect(x, y, w, h, _bg); }

  // Kullanima gore renk: <%50 yesil, <%80 sari, >=%80 kirmizi (statusLine ile ayni).
  uint16_t pctColor(int p) {
    return p >= 80 ? _tft->color565(220, 70, 60)
         : p >= 50 ? _tft->color565(230, 195, 60)
                   : _tft->color565(70, 200, 110);
  }

  // Tek "etiket:%" segmenti ciz, yeni x dondur. pct<0 -> ciz(me), x aynen.
  int drawPct(int x, const char *label, int pct) {
    if (pct < 0) return x;
    char buf[14];
    snprintf(buf, sizeof(buf), "%s%d%%", label, pct);
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(pctColor(pct), _bg);
    _tft->drawString(buf, x, BANDMID_T, 2);
    return x + _tft->textWidth(buf, 2) + 8;
  }

  // Ust bant: model (sonuk gri) + ctx/5h/wk (renkli). Soldan saga dizilir.
  void drawStatus() {
    clearRect(0, 0, 320, 24);
    int x = 6;
    if (_model[0]) {
      _tft->setTextFont(2);
      _tft->setTextDatum(ML_DATUM);
      _tft->setTextColor(_tft->color565(150, 150, 162), _bg);
      _tft->drawString(_model, x, BANDMID_T, 2);
      x += _tft->textWidth(_model, 2) + 10;
    }
    x = drawPct(x, "ctx:", _ctx);
    x = drawPct(x, "5h:",  _h5);
    x = drawPct(x, "wk:",  _wk);
  }

  // Sol-alt: flavor metni, SABIT clawd-turuncu, Font 2.
  void drawAction() {
    clearRect(0, 216, 205, 24);
    if (!_action[0]) return;
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(CLAWD_ORANGE, _bg);
    _tft->drawString(_action, 6, BANDMID_B, 2);
  }

  // Sag-alt: calisan tool adi, sonuk gri, Font 2.
  void drawTool() {
    clearRect(205, 216, 115, 24);
    if (!_tool[0]) return;
    _tft->setTextFont(2);
    _tft->setTextDatum(MR_DATUM);
    _tft->setTextColor(_tft->color565(150, 150, 162), _bg);
    _tft->drawString(_tool, 315, BANDMID_B, 2);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t _bg = 0;
  char     _model[12] = {0};
  int      _ctx = -1, _h5 = -1, _wk = -1;
  char     _action[32] = {0};
  char     _tool[16]   = {0};
  bool     _statusDirty = true, _actionDirty = true, _toolDirty = true;
};
