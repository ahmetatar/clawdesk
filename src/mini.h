#pragma once
// MiniFleet — subagent visualization, shown together with ANIM_AGENTS.
//
// When Claude spawns a subagent, the big clawd shifts up and looks down while a
// row of 4 minis appears along the bottom, each swaying left/right in place
// (mirrored to match its direction). They all disappear when the work finishes.
// The count is always 4, regardless of how many subagents are really running.
//
// The row sits between the clipped ANIM_AGENTS band (y[24,123)) and the HUD
// (y[177,240)), so nothing overlaps. Motion is a deterministic sine sway.

#include <TFT_eSPI.h>
#include "config.h"
#include "anims/clawd_mini.h"

class MiniFleet {
 public:
  static const int MAXM = 4;            // one row of 4

  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = _tft->color565(BG_R, BG_G, BG_B);
    for (int i = 0; i < MAXM; i++) _m[i].active = false;
    _shown = false;
  }

  // Subagent started: show the whole row (idempotent).
  void showAll() {
    if (_shown) return;
    for (int i = 0; i < MAXM; i++) _spawn(i);
    _shown = true;
  }

  // Work finished: erase the row with BG, leaving no residue.
  void clear() {
    for (int i = 0; i < MAXM; i++) {
      if (_m[i].active && _m[i].drawn) _fillRect(_m[i].px, _m[i].py, MW, MH);
      _m[i].active = false;
    }
    _shown = false;
  }

  bool any() const { return _shown; }

  // Call after a full-screen refresh so the "already drawn" state is forgotten.
  void markAllDirty() {
    for (int i = 0; i < MAXM; i++) _m[i].drawn = false;
  }

  // Called every loop; updates and draws at its own pace. Not called while asleep.
  void tick() {
    if (!_shown) return;
    uint32_t now = millis();
    if (now - _lastTick < TICK_MS) return;
    _lastTick = now;
    for (int i = 0; i < MAXM; i++) if (_m[i].active) _step(_m[i]);
  }

 private:
  struct Mini {
    bool     active;
    bool     drawn;      // drawn at least once, so px/py are valid for erasing
    bool     faceLeft;
    int8_t   slot;       // position in the row (0 = leftmost)
    uint16_t ph;         // sway phase (degrees)
    int      px, py;     // last drawn position (for erasing)
  };

  static constexpr int MW = CLAWD_MINI_W, MH = CLAWD_MINI_H;   // 40x40

  static constexpr int ROW_Y  = 130;                 // row y (bottom edge 170 < HUD 177)
  static constexpr int SWAY   = 7;                   // sway amplitude (px)
  static const int HOME_X[MAXM];                     // home x per slot (left edge)
  static const uint32_t TICK_MS  = 40;               // ~25 fps
  static const uint16_t PH_STEP  = 4;                // ~3.6 s per full cycle

  void _spawn(int slot) {
    Mini &m = _m[slot];
    m.active = true;
    m.drawn  = false;
    m.slot   = slot;
    m.ph     = (uint16_t)((slot * 90) % 360);        // phase offset per slot -> wave
    m.faceLeft = false;
  }

  void _step(Mini &m) {
    m.ph = (uint16_t)((m.ph + PH_STEP) % 360);
    float rad = m.ph * 0.01745329f;
    int nx = HOME_X[m.slot] + (int)lroundf(SWAY * sinf(rad));
    int ny = ROW_Y;
    m.faceLeft = cosf(rad) < 0.0f;                   // mirror to the direction of travel

    _drawSprite(nx, ny, m.faceLeft);
    if (m.drawn && nx != m.px) _eraseTrail(m.px, nx, ny);
    m.px = nx; m.py = ny; m.drawn = true;
  }

  // Draw the sprite one row at a time, mirrored when faceLeft.
  void _drawSprite(int x, int y, bool flip) {
    uint16_t row[MW];
    for (int ry = 0; ry < MH; ry++) {
      const uint16_t *src = &clawd_mini[ry * MW];
      if (flip) for (int rx = 0; rx < MW; rx++) row[rx] = src[MW - 1 - rx];
      else      for (int rx = 0; rx < MW; rx++) row[rx] = src[rx];
      _tft->pushImage(x, y + ry, MW, 1, row);
    }
  }

  void _fillRect(int x, int y, int w, int h) { _tft->fillRect(x, y, w, h, _bg); }

  // Erase the sliver the sprite no longer covers after a horizontal move.
  void _eraseTrail(int ox, int nx, int y) {
    if (nx > ox)      _fillRect(ox, y, nx - ox, MH);          // moved right -> trail on the left
    else if (nx < ox) _fillRect(nx + MW, y, ox - nx, MH);     // moved left -> trail on the right
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t  _bg  = 0;
  Mini      _m[MAXM];
  bool      _shown = false;
  uint32_t  _lastTick = 0;
};

// Home x per mini, centred symmetrically around x=160.
const int MiniFleet::HOME_X[MiniFleet::MAXM] = { 41, 107, 173, 239 };
