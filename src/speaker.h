#pragma once
// clawd's "ask" chime — two short rising notes ("di-di") on the CYD's speaker
// jack (GPIO26). The jack is a bare DAC/PWM pin with no onboard amp, so this
// drives a simple square-wave tone via LEDC — a beep, not real audio.
//
// Non-blocking: tick() advances the note sequence off millis(), so playing a
// chime never stalls the main loop's animation/event timing.
#include <Arduino.h>
#include "config.h"

struct SpeakerNote { uint16_t freq; uint16_t ms; }; // freq 0 = silence (gap)

// di-di: a short beep, a small gap, then a slightly higher beep.
static const SpeakerNote SPEAKER_ASK_NOTES[] = { {880, 90}, {0, 40}, {1175, 110} };
constexpr int SPEAKER_ASK_NOTE_COUNT = sizeof(SPEAKER_ASK_NOTES) / sizeof(SPEAKER_ASK_NOTES[0]);

class Speaker {
public:
  void begin() {
    ledcSetup(SPK_CH, 2000, SPK_RES);
    ledcAttachPin(PIN_SPEAKER, SPK_CH);
  }

  // Start the "ask" chime; restarts it if one is already playing.
  void playAsk() {
    _notes = SPEAKER_ASK_NOTES;
    _count = SPEAKER_ASK_NOTE_COUNT;
    _step  = 0;
    _next  = millis();
  }

  // Advance the note sequence. Call every loop; cheap no-op when idle.
  void tick() {
    if (_step >= _count) return;
    uint32_t now = millis();
    if (now < _next) return;
    uint16_t f = _notes[_step].freq;
    if (f) ledcWriteTone(SPK_CH, f); else ledcWrite(SPK_CH, 0);
    _next = now + _notes[_step].ms;
    _step++;
    if (_step >= _count) ledcWrite(SPK_CH, 0); // silence after the last note
  }

private:
  const SpeakerNote *_notes = nullptr;
  int _count = 0;
  int _step  = 0;   // >= _count means idle
  uint32_t _next = 0;
};
