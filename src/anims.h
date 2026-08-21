#pragma once
// Registry of clawd's expression animations. Each one is 8 frames of 64x64
// RGB565, produced by tools/pixellab/clawd_anim.py. Included only by main.cpp
// (the headers are 'static const', so one TU).
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
// anims/clawd_cooking.h is kept as source but is NOT built into the firmware
// (see isWorkPose below); wiring it back up means restoring the include, the
// enum entry and the ANIMS row (~64KB flash).

// Append new animations at the END — ANIMS matches this order, so inserting in
// the middle shifts every existing index.
enum AnimId { ANIM_IDLE, ANIM_HACKING, ANIM_HAPPY, ANIM_THINK, ANIM_OOPS, ANIM_SLEEP, ANIM_ASK, ANIM_AGENTS, ANIM_TICKLE, ANIM_LOVE, ANIM_BRAIN_FULL, ANIM_COMPACT, ANIM_IDLE_MUSIC, ANIM_COUNT };

struct Anim {
  const uint16_t (*frames)[ANIM_W * ANIM_H];  // [count][W*H]
  uint8_t     count;
  uint16_t    interval;   // ms per frame
  bool        transient;  // returns to idle after HOLD_MS
  bool        led_g;      // RGB green while this mood is showing
  bool        led_b;      // RGB blue
  const char *name;
};

static const Anim ANIMS[ANIM_COUNT] = {
  { clawd_idle,    CLAWD_IDLE_FRAMES,    111, false, false, false, "idle"    },
  { clawd_hacking, CLAWD_HACKING_FRAMES, 111, false, true,  false, "hacking" },
  { clawd_happy,   CLAWD_HAPPY_FRAMES,    91, true,  true,  false, "happy"   },
  { clawd_think,   CLAWD_THINK_FRAMES,   200, false, false, true,  "think"   },
  { clawd_oops,    CLAWD_OOPS_FRAMES,     91, true,  false, true,  "oops"    },
  { clawd_sleep,   CLAWD_SLEEP_FRAMES,   200, false, false, false, "sleep"   },  // shown while DIM
  { clawd_ask,     CLAWD_ASK_FRAMES,     130, false, false, true,  "ask"     },  // AskUserQuestion: waiting on you
  { clawd_agents,  CLAWD_AGENTS_FRAMES,  140, false, false, true,  "agents"  },  // subagents: looks up at the minis
  { clawd_tickle,  CLAWD_TICKLE_FRAMES,   80, true,  true,  false, "tickle"  },  // double-tap
  { clawd_love,    CLAWD_LOVE_FRAMES,    110, true,  true,  false, "love"    },  // petting (stroke)
  { clawd_brain_full, CLAWD_BRAIN_FULL_FRAMES, 180, false, false, true, "brain_full" },  // context critical
  { clawd_compact, CLAWD_COMPACT_FRAMES,  150, false, false, true,  "compact" },  // PreCompact
  { clawd_idle_music, CLAWD_IDLE_MUSIC_FRAMES, 125, false, false, false, "idle_music" },  // resting pose while the 5h/weekly quota is exhausted
};

// ---- resting (idle) poses ----
// Pose choice is never random: every pose on the device means something. Resting
// is plain ANIM_IDLE, unless the 5h or weekly usage window is at 100%
// (quotaFull in main.cpp), in which case it's ANIM_IDLE_MUSIC.
static inline bool isIdlePose(AnimId id) { return id == ANIM_IDLE || id == ANIM_IDLE_MUSIC; }

// ---- "working" pose ----
// There is exactly one: the keyboard. tool.pre switches to hacking and stays
// there until the work finishes. (A cooking pose for long jobs was tried and
// reverted; its source still lives in anims/clawd_cooking.h.)
static inline bool isWorkPose(AnimId id) { return id == ANIM_HACKING; }

// How many source rows of this animation to draw. The bottom band is reserved
// for the 3-line HUD, so animations are clipped: y[24,177) for the mascot,
// y[177,240) for the HUD. The clipped rows are empty background below clawd.
// ANIM_AGENTS is clipped harder (33 rows) to leave y[123,177) for the mini row.
static inline uint8_t animPushRows(AnimId id) { return id == ANIM_AGENTS ? 33 : 51; }
