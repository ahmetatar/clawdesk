#pragma once
// clawd ifade animasyonlari kaydi. Her animasyon 8 frame, 64x64 RGB565,
// PixelLab + deterministik kod ile uretildi (tools/pixellab/clawd_anim.py).
// SADECE main.cpp'de include edilir (headerlar 'static const' -> tek TU).
#include <Arduino.h>
#include "config.h"
#include "anims/clawd_idle.h"
#include "anims/clawd_hacking.h"
#include "anims/clawd_happy.h"
#include "anims/clawd_think.h"
#include "anims/clawd_oops.h"
#include "anims/clawd_sleep.h"
#include "anims/clawd_ask.h"
#include "anims/clawd_agents.h"
#include "anims/clawd_tickle.h"
#include "anims/clawd_love.h"

enum AnimId { ANIM_IDLE, ANIM_HACKING, ANIM_HAPPY, ANIM_THINK, ANIM_OOPS, ANIM_SLEEP, ANIM_ASK, ANIM_AGENTS, ANIM_TICKLE, ANIM_LOVE, ANIM_COUNT };

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
};

// Bu anim'in kac SATIRINI ciz. Alt bant HUD 2 satiri (spinner + status line) icin
// bosaltilir: normal anim'ler 58 satira kirpilir (ekran y[24, 24+58*3=198)), altta
// y[198,240) HUD'a kalir -> ust katmanla cakismaz, titreme yok. Kirpilan satirlar
// (58-63) clawd'in ALTINDAKI bos fume'dir; clawd govdesi (row<=47) korunur. hacking
// bile HACK_UP ile yukari kaydigindan klavye row~55'te biter, kirpma disinda kalir.
// ANIM_AGENTS ise yalniz ust bandi (40) cizer; y[144,198) mini zemini, y[198,240) HUD.
static inline uint8_t animPushRows(AnimId id) { return id == ANIM_AGENTS ? 40 : 58; }
