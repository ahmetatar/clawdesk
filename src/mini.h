#pragma once
// MiniFleet — Claude Code ALT-AGENT gorsellestirme (ANIM_AGENTS preset'i ile birlikte).
//
// Claude bir alt-agent (Task/Agent) actiginda buyuk clawd YUKARI kayip ASAGI bakar
// (ANIM_AGENTS) ve en ALTTA, TEK SIRA halinde 4 kucuk clawd BIRDEN belirir.
// Her mini sabit yerinde SAGA-SOLA yumusakca sallanir (yon'e gore aynalanir).
// Alt-agent(ler) bitip is bitince (main.cpp'de gercek sayac 0'a inince) 4'u de kaybolur.
// (Kac gercek alt-agent olursa olsun ekranda hep 4 mini gosterilir — sabit sira.)
//
// CAKISMA YOK: ANIM_AGENTS anim'i yalniz UST bandi cizer (main.cpp animPushRows=33
// -> ekran y[24,123)). Mini sirasi bunun ALTINDA, sabit fume (BG) zeminde (y=SIRAY),
// ve HUD'un 3 satirlik alt bandinin (y[177,240)) DA USTUNDE kalir (alt kenar 170).
// Seffaflik/flicker derdi yok: mini tam kare cizilir, eski konum BG ile silinir.
//
// RANDOM YOK: tum hareket deterministik sinus salinimidir (sakin, ongorulebilir).

#include <TFT_eSPI.h>
#include "config.h"
#include "anims/clawd_mini.h"

class MiniFleet {
 public:
  static const int MAXM = 4;            // en fazla 4 mini, tek sira

  void begin(TFT_eSPI *tft) {
    _tft = tft;
    _bg  = _tft->color565(BG_R, BG_G, BG_B);
    for (int i = 0; i < MAXM; i++) _m[i].active = false;
    _shown = false;
  }

  // Alt-agent(ler) basladi: 4 mini sirasini BIRDEN ac (idempotent — zaten aciksa dokunma).
  void showAll() {
    if (_shown) return;
    for (int i = 0; i < MAXM; i++) _spawn(i);
    _shown = true;
  }

  // Is bitti / oturum sinirlari: 4 mini'yi de topla (BG ile sil, kalinti birakma).
  void clear() {
    for (int i = 0; i < MAXM; i++) {
      if (_m[i].active && _m[i].drawn) _fillRect(_m[i].px, _m[i].py, MW, MH);
      _m[i].active = false;
    }
    _shown = false;
  }

  bool any() const { return _shown; }

  // Tam ekran tazelendiginde (uyanma fillScreen) cagir: eski "cizildi" izini unut.
  void markAllDirty() {
    for (int i = 0; i < MAXM; i++) _m[i].drawn = false;
  }

  // Her loop cagrilir; kendi (sakin) hizinda gunceller+cizer. Uykuda cagrilmaz.
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
    bool     drawn;      // en az bir kez cizildi mi (erase icin px gecerli mi)
    bool     faceLeft;
    int8_t   slot;       // sira icindeki yer (0=en sol)
    uint16_t ph;         // salinim fazi (derece)
    int      px, py;     // en son cizilen tam-sayi konum (erase icin)
  };

  static constexpr int MW = CLAWD_MINI_W, MH = CLAWD_MINI_H;   // 40x40

  // Tek sira: 4 mini ekranin altinda, buyuk clawd'in ALTINDA, ortalanmis ve simetrik.
  // Sol-ust kose ev konumlari (spr 40px, aralik 66 -> satir 41..279, 160 etrafinda ortali).
  static constexpr int ROW_Y  = 130;                 // sabit satir y'si (alt kenar 170 < HUD 177)
  static constexpr int SWAY   = 7;                   // saga-sola salinim genligi (px)
  static const int HOME_X[MAXM];                     // her slotun ev x'i (sol kenar)
  static const uint32_t TICK_MS  = 40;               // ~25 fps
  static const uint16_t PH_STEP  = 4;                // faz artisi/tick -> ~3.6 sn tam dongu

  void _spawn(int slot) {
    Mini &m = _m[slot];
    m.active = true;
    m.drawn  = false;
    m.slot   = slot;
    m.ph     = (uint16_t)((slot * 90) % 360);        // slotlar arasi faz kaymasi -> dalga gorunumu
    m.faceLeft = false;
  }

  void _step(Mini &m) {
    m.ph = (uint16_t)((m.ph + PH_STEP) % 360);
    float rad = m.ph * 0.01745329f;
    int nx = HOME_X[m.slot] + (int)lroundf(SWAY * sinf(rad));
    int ny = ROW_Y;
    m.faceLeft = cosf(rad) < 0.0f;                   // hareket yonune gore aynala

    _drawSprite(nx, ny, m.faceLeft);
    if (m.drawn && nx != m.px) _eraseTrail(m.px, nx, ny);   // yatay hareket -> ince yan seritler
    m.px = nx; m.py = ny; m.drawn = true;
  }

  // Sprite'i tek satir tamponuyla ciz; faceLeft ise yatay aynala.
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

  // Yatay kayma: eski (ox) ile yeni (nx) arasindaki ORTUSMEYEN yan seridi BG ile sil.
  void _eraseTrail(int ox, int nx, int y) {
    if (nx > ox)      _fillRect(ox, y, nx - ox, MH);          // saga gitti -> solda iz
    else if (nx < ox) _fillRect(nx + MW, y, ox - nx, MH);     // sola gitti -> sagda iz
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t  _bg  = 0;
  Mini      _m[MAXM];
  bool      _shown = false;
  uint32_t  _lastTick = 0;
};

// 4 mini ev x'leri (sol kenar): 41,107,173,239 -> satir 160 etrafinda ortali/simetrik.
const int MiniFleet::HOME_X[MiniFleet::MAXM] = { 41, 107, 173, 239 };
