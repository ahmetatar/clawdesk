#pragma once
// UsageScreen — Claude kota gorunumu (sag-ust kose butonuyla normal gorunumle
// yer degistirir). Referans: Claude watch usage ekrani.
//
// YERLESIM (320x240, landscape):
//   ust bant y[0,44):  sol clawd ikonu (clawd_mini 40x40), ORTA buyuk saat (NTP),
//                      sag sarj/pil ikonu (ayni zamanda geri-donus butonunun gorseli).
//   kart 1  y[44,128): "75%" (buyuk beyaz) + "Current" rozeti + bar + "Resets in 4h 22m"
//   kart 2  y[136,220): ayni duzen, "Weekly"
//   alt     y~230:      "* Mustering..." spinner kelimesi (clawd-turuncu, ortali)
//
// VERI: POST /status'tan h5/wk yuzdeleri + h5r/wkr reset stringleri (host "4h22m"
// bicimli yollar; burada "4h 22m" diye aralanir). Spinner kelimesi HUD ile AYNI
// kaynaktan (setAction) beslenir. Saat NTP'den (configTime setup'ta).
//
// CIZIM DISIPLINI: HUD gibi kirli-parca cizimi. FreeFont'lar arkaplan doldurmaz ->
// her parca cizilmeden once kendi zemini (kart/BG) yeniden boyanir. render()
// yalniz uyanikken ve uiUsage aktifken cagrilir.

#include <Arduino.h>
#include <TFT_eSPI.h>   // LOAD_GFXFF: FreeSansBold* fontlari TFT_eSPI.h icinden gelir
#include <time.h>
#include "config.h"
#include "anims/clawd_mini.h"
#include "fonts/clock_font.h"   // saat: FSB24'ten cok az kucuk ozel font (make_clock_font.py)

class UsageScreen {
public:
  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = tft->color565(BG_R, BG_G, BG_B);
  }

  // POST /status'tan yuzdeler + reset stringleri. Degismediyse dokunma (titreme yok).
  void setStatus(int h5, int wk, const char *h5r, const char *wkr) {
    const char *a = h5r ? h5r : "", *b = wkr ? wkr : "";
    if (h5 != _h5 || strcmp(_h5r, a)) { _h5 = h5; strlcpy(_h5r, a, sizeof(_h5r)); _c1Dirty = true; }
    if (wk != _wk || strcmp(_wkr, b)) { _wk = wk; strlcpy(_wkr, b, sizeof(_wkr)); _c2Dirty = true; }
  }

  // Alt spinner kelimesi (HUD setAction ile ayni metin, "Mustering..." bicimli).
  void setAction(const char *txt) {
    const char *t = txt ? txt : "";
    if (!strcmp(_action, t)) return;
    strlcpy(_action, t, sizeof(_action));
    _actDirty = true;
  }

  // Tam yeniden cizim iste (ekrana giris / uykudan uyanis).
  void markAllDirty() { _full = true; }

  // Her loop cagrilir (yalniz usage modunda + uyanikken). Kirli parcalari cizer.
  void render() {
    if (!_tft) return;
    pollClock();
    if (_full) {
      _tft->fillScreen(_bg);
      drawTopBar();
      _c1Dirty = _c2Dirty = _actDirty = _clkDirty = false;
      drawCard(CARD1_Y, "Current", _h5, _h5r);
      drawCard(CARD2_Y, "Weekly",  _wk, _wkr);
      drawSpinner();
      _full = false;
      return;
    }
    if (_clkDirty) { drawClock();   _clkDirty = false; }
    if (_c1Dirty)  { drawCard(CARD1_Y, "Current", _h5, _h5r); _c1Dirty = false; }
    if (_c2Dirty)  { drawCard(CARD2_Y, "Weekly",  _wk, _wkr); _c2Dirty = false; }
    if (_actDirty) { drawSpinner(); _actDirty = false; }
  }

private:
  // -- yerlesim sabitleri --
  static constexpr int CARD_X  = 8,   CARD_W = 304, CARD_H = 84, CARD_R = 10;
  static constexpr int CARD1_Y = 44,  CARD2_Y = 136;
  static constexpr int BAR_X   = 20,  BAR_W  = 280, BAR_H  = 12;
  static constexpr int SPIN_Y  = 230;                       // alt spinner satiri (orta)
  static constexpr int CLOCK_Y = 21;                        // saat dikey ortasi

  // Saati sn'de bir yokla; dakika degisince kirli isaretle. NTP henuz senkron
  // degilse (epoch kucuk) "--:--" gosterilir; senkron olunca kendiliginden dolar.
  void pollClock() {
    uint32_t now = millis();
    if (now - _lastClkPoll < 1000) return;
    _lastClkPoll = now;
    time_t t = time(nullptr);
    int hh = -1, mm = -1;
    if (t > 1600000000) {                                   // NTP geldi mi?
      struct tm tmv;
      localtime_r(&t, &tmv);
      hh = tmv.tm_hour; mm = tmv.tm_min;
    }
    if (hh != _hh || mm != _mm) { _hh = hh; _mm = mm; _clkDirty = true; }
  }

  // "4h22m" -> "4h 22m" (birim harfinden sonra rakam geliyorsa aralik ac). Bos -> "--".
  static void spaceUnits(const char *in, char *out, size_t n) {
    if (!in || !*in) { strlcpy(out, "--", n); return; }
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 2 < n; i++) {
      out[o++] = in[i];
      if (isalpha((unsigned char)in[i]) && isdigit((unsigned char)in[i + 1]) && o + 2 < n)
        out[o++] = ' ';
    }
    out[o] = 0;
  }

  // -- ust bant: ikon + saat + gecis oklari --
  void drawTopBar() {
    _tft->pushImage(8, 2, CLAWD_MINI_W, CLAWD_MINI_H, clawd_mini);   // zemini BG ile ayni
    drawClock();
    drawSwapIcon();
  }

  void drawClock() {
    _tft->fillRect(100, 0, 120, 44, _bg);
    char buf[8];
    if (_hh < 0) strlcpy(buf, "--:--", sizeof(buf));
    else         snprintf(buf, sizeof(buf), "%d:%02d", _hh, _mm);
    _tft->setFreeFont(&ClockBold);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(UI_TEXT_MAIN);
    _tft->drawString(buf, 160, CLOCK_Y);
    _tft->setTextFont(2);                                   // yerlesik fonta don
  }

  // Sag-ust: gorunum-degistirici — HUD'daki cift yonlu takas oklarinin AYNISI,
  // ayni konumda (ayni buton hissi). Hangi modda oldugumuzu ekranin kendisi
  // gosterir; ikon yalniz "gecis yapilir" isaretidir. Dokunma bolgesi genis.
  void drawSwapIcon() {
    const int x = 320 - 32;
    _tft->fillRect(x, 6, 16, 3, CLAWD_ORANGE);                    // ust sap
    _tft->fillTriangle(x + 16, 3, x + 16, 11, x + 23, 7, CLAWD_ORANGE);   // sag ok ucu
    _tft->fillRect(x + 8, 14, 16, 3, CLAWD_ORANGE);               // alt sap
    _tft->fillTriangle(x + 8, 11, x + 8, 19, x + 1, 15, CLAWD_ORANGE);    // sol ok ucu
  }

  // -- kart: buyuk % + rozet + bar + "Resets in ..." --
  void drawCard(int y, const char *label, int pct, const char *rst) {
    _tft->fillRoundRect(CARD_X, y, CARD_W, CARD_H, CARD_R, UI_CARD_BG);

    // buyuk yuzde (sol-ust)
    char pbuf[8];
    if (pct < 0) strlcpy(pbuf, "--%", sizeof(pbuf));
    else         snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    _tft->setFreeFont(&FreeSansBold18pt7b);
    _tft->setTextDatum(TL_DATUM);
    _tft->setTextColor(UI_TEXT_MAIN);
    _tft->drawString(pbuf, CARD_X + 12, y + 8);

    // rozet (sag-ust): "Current" / "Weekly"
    _tft->setFreeFont(&FreeSansBold9pt7b);
    int tw = _tft->textWidth(label);
    int pw = tw + 20, ph = 24;
    int px = CARD_X + CARD_W - 12 - pw, py = y + 8;
    _tft->fillRoundRect(px, py, pw, ph, ph / 2, UI_PILL_BG);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString(label, px + pw / 2, py + ph / 2 - 1);

    // progress bar
    _tft->fillRoundRect(BAR_X, y + 44, BAR_W, BAR_H, BAR_H / 2, UI_BAR_TRACK);
    if (pct > 0) {
      int fw = (int)((long)BAR_W * (pct > 100 ? 100 : pct) / 100);
      if (fw < BAR_H) fw = BAR_H;                           // yuvarlak uclar bozulmasin
      _tft->fillRoundRect(BAR_X, y + 44, fw, BAR_H, BAR_H / 2, UI_BAR_FILL);
    }

    // "Resets in 4h 22m"
    char sp[16], rbuf[28];
    spaceUnits(rst, sp, sizeof(sp));
    snprintf(rbuf, sizeof(rbuf), "Resets in %s", sp);
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(UI_TEXT_SOFT, UI_CARD_BG);
    _tft->drawString(rbuf, BAR_X, y + 68, 2);
  }

  // alt satir: "* Mustering..." (clawd-turuncu, ortali). Bos action -> satir bos.
  void drawSpinner() {
    _tft->fillRect(0, SPIN_Y - 9, 320, 19, _bg);
    if (!_action[0]) return;
    char buf[44];
    snprintf(buf, sizeof(buf), "* %s", _action);
    _tft->setTextFont(2);
    _tft->setTextDatum(MC_DATUM);
    _tft->setTextColor(CLAWD_ORANGE, _bg);
    _tft->drawString(buf, 160, SPIN_Y, 2);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t _bg = 0;
  int  _h5 = -1, _wk = -1;
  char _h5r[10] = {0}, _wkr[10] = {0};
  char _action[40] = {0};
  int  _hh = -1, _mm = -1;
  uint32_t _lastClkPoll = 0;
  bool _full = true, _c1Dirty = false, _c2Dirty = false, _actDirty = false, _clkDirty = false;
};
