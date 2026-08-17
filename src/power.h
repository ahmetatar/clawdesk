#pragma once
// clawd power management — two idle stages plus a soft backlight fade.
//
//   ACTIVE  (full brightness)          <- any event or touch returns here
//     | no events for T_DIM_MS
//   DIM     (~11%, animation keeps running)
//     | no events for T_SLEEP_MS
//   SLEEP   (backlight off, animation stopped, CPU 80MHz, WiFi modem-sleep)
//
// WiFi stays up, so an incoming POST /e wakes the CPU and main loop calls
// notifyActivity(). This class owns backlight and timing only; main.cpp applies
// the animation/CPU/WiFi side effects on state edges.
#include <Arduino.h>
#include "config.h"

class PowerManager {
public:
  enum State { ACTIVE, DIM, SLEEP };

  void begin() {
    ledcSetup(BL_CH, BL_FREQ, BL_RES);
    ledcAttachPin(PIN_BL, BL_CH);   // must run AFTER tft.init(), which claims the BL pin
    _cur = _target = BL_FULL;
    ledcWrite(BL_CH, _cur);
    _state = ACTIVE;
    _lastActivity = millis();
    _lastRamp = _lastActivity;
  }

  // An event or touch happened: reset the idle timer.
  void notifyActivity() { _lastActivity = millis(); }

  // Is Claude busy? While true, dim/sleep is disabled. The safety cap is
  // measured from when busy STARTED, so a steady trickle of events cannot
  // extend it forever.
  void setBusy(bool b) {
    if (b && !_busy) _busySince = millis();
    _busy = b;
  }

  State state() const { return _state; }
  bool  asleep() const { return _state == SLEEP; }

  // Called every loop: updates the state and fades the backlight toward its
  // target. Returns true when the state changed on this tick.
  bool tick() {
    uint32_t now = millis();
    uint32_t idle = now - _lastActivity;

    // Stay awake while busy, up to the safety cap; past the cap (a lost Stop)
    // fall back to the normal idle timers.
    bool hold = _busy && (now - _busySince < T_BUSY_MAX_MS);

    State next = hold                 ? ACTIVE
               : (idle >= T_SLEEP_MS) ? SLEEP
               : (idle >= T_DIM_MS)   ? DIM
                                      : ACTIVE;
    bool changed = (next != _state);
    _state = next;
    _target = (next == SLEEP) ? BL_OFF : (next == DIM) ? BL_DIM : BL_FULL;

    // soft, non-blocking fade
    if (_cur != _target && (now - _lastRamp) >= BL_RAMP_MS) {
      _lastRamp = now;
      if (_cur < _target) _cur = (_target - _cur > BL_STEP) ? _cur + BL_STEP : _target;
      else                _cur = (_cur - _target > BL_STEP) ? _cur - BL_STEP : _target;
      ledcWrite(BL_CH, _cur);
    }
    return changed;
  }

private:
  State    _state       = ACTIVE;
  bool     _busy         = false;    // Claude is working (until Stop)
  uint32_t _busySince    = 0;        // where the safety cap is measured from
  uint32_t _lastActivity = 0;
  uint32_t _lastRamp     = 0;
  uint16_t _cur          = BL_FULL;   // 0..255, 16-bit for intermediate values
  uint16_t _target       = BL_FULL;
};
