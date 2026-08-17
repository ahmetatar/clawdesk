#pragma once
// UsageScreen — the Claude quota view, swapped in by the top-right corner button.
//
// Layout (320x240, landscape):
//   top bar y[0,44):  clawd icon left, big NTP clock centre, view toggle right.
//   card 1  y[44,128): "75%" + "Current" badge + bar + "Resets in 4h 22m"
//   card 2  y[136,220): same layout, "Weekly"
//   bottom  y~230:      centred spinner word
//
// Data: h5/wk percentages and h5r/wkr reset strings from POST /status (the host
// sends "4h22m", spaced out here). The spinner word comes from setAction, the
// same source the HUD uses; the clock comes from NTP.
//
// Drawing discipline: dirty-part rendering like the HUD. FreeFonts do not fill
// their background, so each part repaints its own backdrop first.

#include <Arduino.h>
#include <TFT_eSPI.h>   // LOAD_GFXFF pulls in the FreeSansBold* fonts
#include <time.h>
#include "config.h"
#include "ui_toggle.h"
#include "spinner_fx.h"
#include "anims/clawd_mini.h"
#include "fonts/clock_font.h"   // custom clock font, slightly smaller than FSB24

class UsageScreen {
public:
  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = tft->color565(BG_R, BG_G, BG_B);
    _fx.begin(tft, _bg);
  }

  // Percentages and reset strings from POST /status; unchanged values are ignored.
  void setStatus(int h5, int wk, const char *h5r, const char *wkr) {
    const char *a = h5r ? h5r : "", *b = wkr ? wkr : "";
    if (h5 != _h5 || strcmp(_h5r, a)) { _h5 = h5; strlcpy(_h5r, a, sizeof(_h5r)); _c1Dirty = true; }
    if (wk != _wk || strcmp(_wkr, b)) { _wk = wk; strlcpy(_wkr, b, sizeof(_wkr)); _c2Dirty = true; }
  }

  // Bottom spinner word — same text and state the HUD's setAction receives.
  void setAction(const char *txt, bool spin = false) {
    const char *t = txt ? txt : "";
    if (!strcmp(_action, t) && spin == _spin) return;
    strlcpy(_action, t, sizeof(_action));
    _spin = spin;
    _actDirty = true;
  }

  // Request a full redraw (entering this view / waking up).
  void markAllDirty() { _full = true; }

  // Called every loop while this view is active and awake; draws dirty parts.
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
      drawSwapIcon();          // last: its slide must not delay the cards
      _full = false;
      return;
    }
    if (_clkDirty) { drawClock();   _clkDirty = false; }
    if (_c1Dirty)  { drawCard(CARD1_Y, "Current", _h5, _h5r); _c1Dirty = false; }
    if (_c2Dirty)  { drawCard(CARD2_Y, "Weekly",  _wk, _wkr); _c2Dirty = false; }
    if (_actDirty) { drawSpinner(); _actDirty = false; }
    _fx.tick();
  }

private:
  // -- layout --
  static constexpr int CARD_X  = 8,   CARD_W = 304, CARD_H = 84, CARD_R = 10;
  static constexpr int CARD1_Y = 44,  CARD2_Y = 136;
  static constexpr int BAR_X   = 20,  BAR_W  = 280, BAR_H  = 12;
  static constexpr int SPIN_Y  = 230;                       // bottom spinner line
  static constexpr int CLOCK_Y = 21;                        // clock vertical centre

  // Poll the clock once a second; mark dirty when the minute changes. Shows
  // "--:--" until NTP has synced.
  void pollClock() {
    uint32_t now = millis();
    if (now - _lastClkPoll < 1000) return;
    _lastClkPoll = now;
    time_t t = time(nullptr);
    int hh = -1, mm = -1;
    if (t > 1600000000) {                                   // NTP synced?
      struct tm tmv;
      localtime_r(&t, &tmv);
      hh = tmv.tm_hour; mm = tmv.tm_min;
    }
    if (hh != _hh || mm != _mm) { _hh = hh; _mm = mm; _clkDirty = true; }
  }

  // "4h22m" -> "4h 22m"; empty input becomes "--".
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

  // -- top bar: icon + clock + toggle --
  void drawTopBar() {
    _tft->pushImage(8, 2, CLAWD_MINI_W, CLAWD_MINI_H, clawd_mini);
    drawClock();          // the toggle is drawn last, at the end of render()
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
    _tft->setTextFont(2);                                   // back to the built-in font
  }

  // Top-right: the same toggle the HUD draws, here in its ON state.
  void drawSwapIcon() { drawUiToggle(_tft, _bg, true); }

  // -- card: big % + badge + bar + "Resets in ..." --
  void drawCard(int y, const char *label, int pct, const char *rst) {
    _tft->fillRoundRect(CARD_X, y, CARD_W, CARD_H, CARD_R, UI_CARD_BG);

    // big percentage (top-left)
    char pbuf[8];
    if (pct < 0) strlcpy(pbuf, "--%", sizeof(pbuf));
    else         snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    _tft->setFreeFont(&FreeSansBold18pt7b);
    _tft->setTextDatum(TL_DATUM);
    _tft->setTextColor(UI_TEXT_MAIN);
    _tft->drawString(pbuf, CARD_X + 12, y + 8);

    // badge (top-right): "Current" / "Weekly"
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
      if (fw < BAR_H) fw = BAR_H;                           // keep the rounded caps intact
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

  // Bottom line: the HUD's spinner line, centred here.
  void drawSpinner() {
    _fx.stop();
    _tft->fillRect(0, SPIN_Y - 10, 320, 20, _bg);
    if (!_action[0]) return;
    _fx.set(_action, _spin);
    _fx.draw(160, SPIN_Y, true);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t _bg = 0;
  int  _h5 = -1, _wk = -1;
  char _h5r[10] = {0}, _wkr[10] = {0};
  char _action[40] = {0};
  bool _spin = false;              // is the text in spinner mode (mark + dots)
  SpinnerFx _fx;
  int  _hh = -1, _mm = -1;
  uint32_t _lastClkPoll = 0;
  bool _full = true, _c1Dirty = false, _c2Dirty = false, _actDirty = false, _clkDirty = false;
};
