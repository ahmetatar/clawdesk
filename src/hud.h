#pragma once
// clawd HUD — a light layer that writes three lines in the band below clawd.
// Animations are clipped to y[24,177), leaving y[177,240) to the HUD.
//
// Layout:
//   top-left  (y[0,24)): WiFi signal bars from RSSI (connected green, lost red).
//   line 1 (y~188): spinner / flavor text ("Thinking...", "Crafting..."), orange.
//   line 2 (y~208): reset countdowns — "5h: (2h32m)   wk: (2d5h)", dim grey.
//   line 3 (y~228): status line summary — "Model  ctx:%  5h:%  wk:%", with the
//                   percentages colored by usage (green<50, yellow<80, red>=80).
//
// Data: statusLine -> POST /status; flavor text -> setAction (from events).
// Drawing discipline: a line is redrawn only when it changes.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "ui_toggle.h"
#include "spinner_fx.h"

class Hud {
public:
  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = tft->color565(BG_R, BG_G, BG_B);
    _fx.begin(tft, _bg);
    _wifiDirty = _statusDirty = _actionDirty = _resetDirty = _btnDirty = true;
  }

  // rssi: WiFi.RSSI() in dBm. connected=false -> a single red bar.
  // Called periodically; redraws only when the bar count or state changes.
  void setWifi(bool connected, int rssi) {
    int bars = !connected     ? 1
             : rssi >= -60     ? 4
             : rssi >= -68     ? 3
             : rssi >= -76     ? 2
                               : 1;
    if (connected == _connected && bars == _bars) return;
    _connected = connected;
    _bars = bars;
    _wifiDirty = true;
  }

  // Status line summary (bottom row). Empty model -> "Claude".
  // ctx/h5/wk are percentages; <0 draws a dim "label -" placeholder.
  void setStatus(const char *model, int ctx, int h5, int wk) {
    const char *m = model ? model : "";
    // Dedup on the device: drawStatus clears its band, so redrawing an unchanged
    // value would show as a visible blink. markAllDirty() bypasses this, so a
    // wake-up or first boot always draws.
    if (ctx == _ctx && h5 == _h5 && wk == _wk && !strcmp(_model, m)) return;
    strlcpy(_model, m, sizeof(_model));
    _ctx = ctx; _h5 = h5; _wk = wk;
    _statusDirty = true;
  }

  // Spinner / flavor text (line 1), in clawd-orange; empty clears it.
  // spin=true adds the pulsing mark and travelling dots; spin=false draws plain
  // text ("Shipping!", "Oops..."). Repeating the same text is a no-op.
  void setAction(const char *txt, bool spin = false) {
    const char *t = txt ? txt : "";
    if (!strcmp(_action, t) && spin == _spin) return;
    strlcpy(_action, t, sizeof(_action));
    _spin = spin;
    _actionDirty = true;
  }

  // Reset countdowns (line 2, grey): "5h: (2h32m)  wk: (2d5h)". The host sends
  // pre-formatted strings; empty ones fall back to "-".
  void setReset(const char *h5r, const char *wkr) {
    const char *a = h5r ? h5r : "", *b = wkr ? wkr : "";
    if (!strcmp(_h5r, a) && !strcmp(_wkr, b)) return;
    strlcpy(_h5r, a, sizeof(_h5r));
    strlcpy(_wkr, b, sizeof(_wkr));
    _resetDirty = true;
  }

  void markAllDirty() { _wifiDirty = _statusDirty = _actionDirty = _resetDirty = _btnDirty = true; }

  // Called every loop; draws only dirty lines. Not called while asleep.
  void render() {
    if (!_tft) return;
    if (_wifiDirty)   { drawWifi();   _wifiDirty   = false; }
    if (_actionDirty) { drawAction(); _actionDirty = false; }
    if (_resetDirty)  { drawReset();  _resetDirty  = false; }
    if (_statusDirty) { drawStatus(); _statusDirty = false; }
    if (_btnDirty)    { drawBtn();    _btnDirty    = false; }   // last: its slide must not delay the rest
    _fx.tick();
  }

private:
  static constexpr int SPIN_Y   = 188;    // line 1 (spinner/flavor), vertical centre
  static constexpr int RESET_Y  = 208;    // line 2 (reset countdowns)
  static constexpr int STATUS_Y = 228;    // line 3 (status line)

  void clearRect(int x, int y, int w, int h) { _tft->fillRect(x, y, w, h, _bg); }

  // Top-left: 4 signal bars, _bars of them filled.
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

  // Top-right: the view toggle in its OFF state (this is the mascot view).
  // The usage view draws the same toggle turned on.
  void drawBtn() { drawUiToggle(_tft, _bg, false); }

  // Usage color: <50 green, <80 yellow, >=80 red (same as the statusLine).
  uint16_t pctColor(int p) {
    return p >= 80 ? _tft->color565(220, 70, 60)
         : p >= 50 ? _tft->color565(230, 195, 60)
                   : _tft->color565(70, 200, 110);
  }

  // Draw one "label %" segment and return the next x. pct<0 draws a dim
  // placeholder so the row is never empty.
  int drawPct(int x, const char *label, int pct) {
    char buf[14];
    uint16_t col;
    if (pct < 0) {
      snprintf(buf, sizeof(buf), "%s -", label);          // "ctx: -" (awaiting data)
      col = _tft->color565(120, 120, 132);
    } else {
      snprintf(buf, sizeof(buf), "%s %d%%", label, pct);  // "ctx: 12%"
      col = pctColor(pct);
    }
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(col, _bg);
    _tft->drawString(buf, x, STATUS_Y, 2);
    return x + _tft->textWidth(buf, 2) + 8;
  }

  // Line 1: SpinnerFx owns the layout; here we just clear the band and draw once.
  // 260 px covers mark + gap + the longest gerund + dots.
  void drawAction() {
    _fx.stop();                              // stop the old line animating over the new background
    clearRect(0, SPIN_Y - 10, 260, 20);
    if (!_action[0]) return;
    _fx.set(_action, _spin);
    _fx.draw(6, SPIN_Y, false);
  }

  // Line 2: reset countdowns in dim grey — informational, never colored by usage.
  void drawReset() {
    clearRect(0, RESET_Y - 10, 320, 20);
    if (!_h5r[0] && !_wkr[0]) return;
    char buf[40];
    snprintf(buf, sizeof(buf), "5h: (%s)   wk: (%s)",
             _h5r[0] ? _h5r : "-", _wkr[0] ? _wkr : "-");
    _tft->setTextFont(2);
    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(_tft->color565(120, 120, 132), _bg);
    _tft->drawString(buf, 6, RESET_Y, 2);
  }

  // Line 3: model (dim grey) followed by the colored ctx/5h/wk segments.
  void drawStatus() {
    clearRect(0, STATUS_Y - 11, 320, 23);
    int x = 6;
    const char *model = _model[0] ? _model : "Claude";
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
  bool     _spin = false;          // is the text in spinner mode (mark + dots)
  SpinnerFx _fx;
  char     _h5r[10] = {0}, _wkr[10] = {0};
  bool     _connected  = false;
  int      _bars       = 1;
  bool     _wifiDirty = true, _statusDirty = true, _actionDirty = true, _resetDirty = true,
           _btnDirty = true;
};
