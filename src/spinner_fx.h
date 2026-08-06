#pragma once
// clawd SPINNER EFEKTI — Claude Code CLI'daki spinner satirinin cihaz karsiligi:
//
//     [*]  Cogitating  . . .
//      ^      ^         ^
//      |      |         +-- 3 nokta, sirayla dalga halinde parlar (soldan saga akar)
//      |      +------------ spinner kelimesi (CC'nin GERCEK gerund havuzundan)
//      +------------------- Claude yildizi (asterisk mark), yumusak nabiz atar
//
// TITREME/FLIP YOK — tasarim kurali:
//   * Kelime YALNIZ metin degisince cizilir (draw()); animasyon ona hic dokunmaz.
//   * Yildiz ve noktalar sabit piksellere SAHIP; animasyon o pikselleri sadece
//     YENIDEN RENKLENDIRIR (arkasi asla fillRect ile silinmez) -> bir kare bile
//     "bos" gorunmez, dolayisiyla goz titreme/blink algilamaz.
//   * Parlaklik 16 kademeye yuvarlanir; kademe DEGISMEDIKCE SPI'ye hic yazilmaz.
//   * Renk hep zemin(BG) -> clawd-turuncu arasi bir karisimdir; en sonuk kademede
//     bile %30 gorunur kalir (tam sondurme = "blink", istenen degil).
//
// Kullanim: begin() bir kez; metin degisince set()+draw(); loop'ta her tur tick().

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>
#include "config.h"

// Claude yildizi, 9x9 (elle cizilmis): tam boy dikey+yatay isin + 4 capraz isin.
// Boyut, yanindaki Font2 metninin govde yuksekligiyle (~11px) uyumlu kalsin diye
// kucuk tutuldu — daha buyugu satirda "ikon" degil "amblem" gibi duruyordu.
static const int MARK_N = 9;
static const char *const CLAUDE_MARK[MARK_N] = {
  "....#....",
  "....#....",
  ".#..#..#.",
  "..#.#.#..",
  "#########",
  "..#.#.#..",
  ".#..#..#.",
  "....#....",
  "....#....",
};

class SpinnerFx {
public:
  static constexpr int ICON_W   = MARK_N;   // yildiz genisligi/yuksekligi
  static constexpr int ICON_GAP = 6;        // yildiz ile kelime arasi
  static constexpr int DOT_GAP  = 5;    // kelime ile ilk nokta arasi
  static constexpr int DOT_STEP = 5;    // noktalar arasi mesafe
  static constexpr int DOT_SZ   = 2;    // nokta kenari (px)
  static constexpr int DOTS_W   = DOT_SZ + 2 * DOT_STEP;

  void begin(TFT_eSPI *tft, uint16_t bg) { _tft = tft; _bg = bg; _on = false; }

  // txt : gosterilecek metin ("Cogitating" ya da "Shipping!")
  // anim: true  -> spinner durumu (yildiz + hareketli noktalar),
  //       false -> duz statik flavor metni (ne yildiz ne nokta).
  void set(const char *txt, bool anim) {
    strlcpy(_txt, txt ? txt : "", sizeof(_txt));
    _anim = anim;
  }

  // Toplam genislik (yerlesim hesabi icin; cagiran temizleyecegi bandi buna gore secebilir).
  int width() {
    if (!_txt[0]) return 0;
    _tft->setTextFont(2);
    int tw = _tft->textWidth(_txt, 2);
    return _anim ? (ICON_W + ICON_GAP + tw + DOT_GAP + DOTS_W) : tw;
  }

  // Satiri BIR KEZ ciz (metin degisince cagrilir; zemin cagiran tarafindan
  // temizlenmis olmali). centered=false -> x sol kenar; true -> x satirin merkezi.
  void draw(int x, int cy, bool centered) {
    _on = false;
    if (!_tft || !_txt[0]) return;
    _tft->setTextFont(2);
    int tw    = _tft->textWidth(_txt, 2);
    int total = width();
    int left  = centered ? x - total / 2 : x;
    int tx    = left + (_anim ? ICON_W + ICON_GAP : 0);

    _tft->setTextDatum(ML_DATUM);
    _tft->setTextColor(CLAWD_ORANGE, _bg);
    _tft->drawString(_txt, tx, cy, 2);
    if (!_anim) return;

    _iconX = left;
    _iconY = cy - ICON_W / 2;
    _dotX  = tx + tw + DOT_GAP;
    _dotY  = cy + 4;                       // taban cizgisi hizasi
    _iconStep = _dotStep[0] = _dotStep[1] = _dotStep[2] = -1;
    _on = true;
    tick(true);                            // ilk kareyi hemen bas (bos an olmasin)
  }

  // Her loop turunda cagrilir. Yalniz parlaklik KADEMESI degisen parcayi yeniden
  // renklendirir; kademe ayniysa hic SPI trafigi olmaz.
  void tick(bool force = false) {
    if (!_on) return;
    uint32_t ms = millis();

    int s = level(ms, PULSE_MS, 0.0f);
    if (force || s != _iconStep) { _iconStep = s; drawIcon(shade(s)); }

    for (int i = 0; i < 3; i++) {
      // Faz kaymasi -> parlaklik dalgasi soldan saga akar ("noktalar ilerliyor").
      int d = level(ms, DOT_MS, -0.18f * i);
      if (force || d != _dotStep[i]) { _dotStep[i] = d; drawDot(i, shade(d)); }
    }
  }

  // Satir gizlendi/uzeri baska sey cizildi -> animasyon dursun (tick no-op olsun).
  void stop() { _on = false; }

private:
  static constexpr uint32_t PULSE_MS = 1500;   // yildiz nabzi (tam tur)
  static constexpr uint32_t DOT_MS   = 1100;   // nokta dalgasi (tam tur)
  static constexpr int      STEPS    = 16;     // parlaklik kademesi
  static constexpr float    FLOOR    = 0.30f;  // en sonuk kademe (0 = tam sonme; istemiyoruz)

  // ms'yi 0..STEPS-1 parlaklik kademesine cevir (cosinus -> yumusak gidis-gelis).
  static int level(uint32_t ms, uint32_t period, float shift) {
    float p = (float)(ms % period) / (float)period + shift;
    p -= floorf(p);
    float k = 0.5f - 0.5f * cosf(p * 6.28318531f);
    return (int)(k * (STEPS - 1) + 0.5f);
  }

  // Kademe -> zemin ile clawd-turuncu arasi karisim rengi.
  uint16_t shade(int s) {
    float t = FLOOR + (1.0f - FLOOR) * (float)s / (float)(STEPS - 1);
    uint8_t r = (uint8_t)(BG_R + (213 - BG_R) * t);
    uint8_t g = (uint8_t)(BG_G + (82  - BG_G) * t);
    uint8_t b = (uint8_t)(BG_B + (56  - BG_B) * t);
    return RGB565(r, g, b);
  }

  // Yildizin YALNIZ dolu piksellerini yeniden boyar (bos pikseller hic dokunulmaz
  // -> zemin silinmez -> titreme yok). Ardisik pikseller tek HLine'a toplanir.
  void drawIcon(uint16_t col) {
    for (int y = 0; y < MARK_N; y++) {
      const char *row = CLAUDE_MARK[y];
      int x = 0;
      while (x < MARK_N) {
        if (row[x] != '#') { x++; continue; }
        int run = 0;
        while (x + run < MARK_N && row[x + run] == '#') run++;
        _tft->drawFastHLine(_iconX + x, _iconY + y, run, col);
        x += run;
      }
    }
  }

  void drawDot(int i, uint16_t col) {
    _tft->fillRect(_dotX + i * DOT_STEP, _dotY, DOT_SZ, DOT_SZ, col);
  }

  TFT_eSPI *_tft = nullptr;
  uint16_t  _bg  = 0;
  char      _txt[40] = {0};
  bool      _anim = false, _on = false;
  int       _iconX = 0, _iconY = 0, _dotX = 0, _dotY = 0;
  int       _iconStep = -1, _dotStep[3] = {-1, -1, -1};
};
