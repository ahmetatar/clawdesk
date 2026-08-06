#pragma once
// clawd ifade animasyonlari kaydi. Her animasyon 8 frame, 64x64 RGB565,
// PixelLab + deterministik kod ile uretildi (tools/pixellab/clawd_anim.py).
// SADECE main.cpp'de include edilir (headerlar 'static const' -> tek TU).
#include <Arduino.h>
#include "config.h"
#include "anims/clawd_idle.h"
#include "anims/clawd_idle_music.h"
#include "anims/clawd_hacking.h"
#include "anims/clawd_happy.h"
#include "anims/clawd_think.h"
#include "anims/clawd_oops.h"
#include "anims/clawd_sleep.h"
#include "anims/clawd_ask.h"
#include "anims/clawd_agents.h"
#include "anims/clawd_tickle.h"
#include "anims/clawd_love.h"
#include "anims/clawd_brain_full.h"
#include "anims/clawd_compact.h"
#include "anims/clawd_cooking.h"

// NOT: yeni anim'i SONA ekle (ANIMS dizisi bu sirayla eslesir; araya girmek
// mevcut tum indeksleri kaydirir).
enum AnimId { ANIM_IDLE, ANIM_HACKING, ANIM_HAPPY, ANIM_THINK, ANIM_OOPS, ANIM_SLEEP, ANIM_ASK, ANIM_AGENTS, ANIM_TICKLE, ANIM_LOVE, ANIM_BRAIN_FULL, ANIM_COMPACT, ANIM_IDLE_MUSIC, ANIM_COOKING, ANIM_COUNT };

struct Anim {
  const uint16_t (*frames)[ANIM_W * ANIM_H];  // [count][W*H]
  uint8_t     count;
  uint16_t    interval;   // frame basi ms (fps'ten)
  bool        transient;  // true ise HOLD_MS sonra idle'a doner
  bool        led_g;      // RGB yesil (bu ifade suresince)
  bool        led_b;      // RGB mavi
  const char *name;
};

// fps (project-status): idle 9, hacking 9, happy 11, think 5, oops 11
static const Anim ANIMS[ANIM_COUNT] = {
  { clawd_idle,    CLAWD_IDLE_FRAMES,    111, false, false, false, "idle"    },
  { clawd_hacking, CLAWD_HACKING_FRAMES, 111, false, true,  false, "hacking" },
  { clawd_happy,   CLAWD_HAPPY_FRAMES,    91, true,  true,  false, "happy"   },
  { clawd_think,   CLAWD_THINK_FRAMES,   200, false, false, true,  "think"   },
  { clawd_oops,    CLAWD_OOPS_FRAMES,     91, true,  false, true,  "oops"    },
  { clawd_sleep,   CLAWD_SLEEP_FRAMES,   200, false, false, false, "sleep"   },  // DIM'de: uyuklama pozu
  { clawd_ask,     CLAWD_ASK_FRAMES,     130, false, false, true,  "ask"     },  // AskUserQuestion: "?" seni bekliyor
  { clawd_agents,  CLAWD_AGENTS_FRAMES,  140, false, false, true,  "agents"  },  // alt-agent: yukari kayip mini'lere bakar
  { clawd_tickle,  CLAWD_TICKLE_FRAMES,   80, true,  true,  false, "tickle"  },  // CIFT-DOKUNUS: gidiklanma (> < + hizli titreme)
  { clawd_love,    CLAWD_LOVE_FRAMES,    110, true,  true,  false, "love"    },  // OKSAMA (surtme): > < + yukselen kalpler
  { clawd_brain_full, CLAWD_BRAIN_FULL_FRAMES, 180, false, false, true, "brain_full" },  // context kritik: beyin bardak gibi dolup bosalir
  { clawd_compact, CLAWD_COMPACT_FRAMES,  150, false, false, true,  "compact" },  // PreCompact: beyin + minik yanip sonen yildizlar
  { clawd_idle_music, CLAWD_IDLE_MUSIC_FRAMES, 125, false, false, false, "idle_music" },  // kulaklikla muzik — su an HICBIR olay tetiklemiyor (bkz. isIdlePose notu)
  { clawd_cooking, CLAWD_COOKING_FRAMES,      111, false, true,  false, "cooking" },  // UZAYAN is: klavye WORK_LONG_MS'i asinca tavaya gecer (LED yesil, hacking gibi)
};

// ---- dinlenme (idle) pozlari ----
// Maskot secimi ARTIK RASTGELE DEGIL: cihazdaki her poz bir seyi anlatir, hicbiri
// dekor degil (bkz. hacking -> cooking yukseltmesi). Bu yuzden agirlikli idle havuzu
// (IDLE_POOL/pickIdle) kaldirildi; "dinlenme" = sade ANIM_IDLE.
//
// ANIM_IDLE_MUSIC (kulaklikla muzik) KAYITTA DURUYOR ama su an HICBIR olay onu
// tetiklemiyor — hangi duruma baglanacagi henuz kararlastirilmadi (en guclu aday:
// session.stop = "tur bitti, sira sende"). Bagladigimizda tek yapilacak sey
// mapEvent'te ilgili satiri ANIM_IDLE_MUSIC'e cevirmek.
//
// isIdlePose muzigi de KAPSAR: uc yerde kullaniliyor (dokunmatik tickle/love sonrasi
// donus, brain_full takasi, HUD kategorisi). Muzik "dinlenme pozu" sayilmazsa,
// gosterildigi gun clawd'i oksadiginda muzige degil sade idle'a doner — sessizce
// bozulan cinsten bir hata. Simdiden dogru tanimla dursun.
static inline bool isIdlePose(AnimId id) { return id == ANIM_IDLE || id == ANIM_IDLE_MUSIC; }

// ---- "calisiyor" pozlari: klavye -> (uzarsa) tava ----
// Calisma pozu RASTGELE SECILMEZ. tool.pre her zaman klavyeyle baslar; is idle'a
// donmeden WORK_LONG_MS'i asarsa maskot tavaya gecer ve is bitene kadar orada kalir
// (bkz. main.cpp workSince). Yani tava bir SURPRIZ degil, BILGI: "bu is uzadi,
// kaynatiyor". Gecis tek yonlu oldugu icin pes pese tool cagrilarinda takas/titreme
// olmaz — agirlikli rastgele secimin (eski WORK_POOL) asil sorunu buydu.
static inline bool isWorkPose(AnimId id) { return id == ANIM_HACKING || id == ANIM_COOKING; }

// Bu anim'in kac SATIRINI ciz. Alt bant HUD 3 satiri (spinner + reset-sayaci + status
// line) icin bosaltilir: normal anim'ler 51 satira kirpilir (ekran y[24, 24+51*3=177)),
// altta y[177,240) HUD'a kalir -> ust katmanla cakismaz, titreme yok. Kirpilan satirlar
// (51-63) clawd'in ALTINDAKI bos fume'dir; clawd govdesi (row<=47) korunur. hacking
// HACK_UP=16 ile yukari kaydigindan klavye row~47'de biter, kirpma disinda kalir.
// ANIM_AGENTS ise yalniz ust bandi cizer; HUD 2->3 satira cikinca bu bant da 40'tan
// 33'e daraltildi (clawd_anim.py AGENTS_UP 8->16 ile uyumlu, feet row~31 korunur) ki
// mini sirasina (mini.h ROW_Y=130, alt kenar 170) y[123,177) araliginda yer acilsin:
// y[24,123) buyuk clawd, y[123,177) mini zemini, y[177,240) HUD (3 satir).
static inline uint8_t animPushRows(AnimId id) { return id == ANIM_AGENTS ? 33 : 51; }
