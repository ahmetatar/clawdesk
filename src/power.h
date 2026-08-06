#pragma once
// clawd guc yonetimi — iki kademeli idle durumu + yumusak arka isik fade.
//
//   ACTIVE  (tam parlaklik)                          <- her event/dokunma buraya doner
//     | T_DIM_MS boyunca olay yok
//   DIM     (kisik ~%11, animasyon doner)
//     | T_SLEEP_MS boyunca olay yok
//   SLEEP   (arka isik 0, animasyon durur, CPU 80MHz, WiFi modem-sleep)
//
// WiFi ayakta kaldigi icin gelen POST /e paketi CPU'yu uyandirir; main loop
// olayi kuyruktan alirken notifyActivity() cagirir -> ACTIVE'e doneriz.
// Bu sinif SADECE arka isik + zamanlamayi yonetir; animasyon/CPU/WiFi yan
// etkilerini main.cpp durum kenarlarina (edge) gore uygular.
#include <Arduino.h>
#include "config.h"

class PowerManager {
public:
  enum State { ACTIVE, DIM, SLEEP };

  void begin() {
    ledcSetup(BL_CH, BL_FREQ, BL_RES);
    ledcAttachPin(PIN_BL, BL_CH);   // tft.init()'ten SONRA cagir (BL pinini devralir)
    _cur = _target = BL_FULL;
    ledcWrite(BL_CH, _cur);
    _state = ACTIVE;
    _lastActivity = millis();
    _lastRamp = _lastActivity;
  }

  // Bir olay/dokunma oldu: idle sayacini sifirla (tick ACTIVE'e cekecek).
  void notifyActivity() { _lastActivity = millis(); }

  // Claude mesgul mu? true iken uyku/kisma kapali (Stop gelene kadar).
  // main.cpp her olayda cagirir (siniflandirma orada: bitiren olaylar -> false).
  // _busySince = busy'nin BASLADIGI an; emniyet tavani buradan olculur (son olaydan
  // DEGIL) -> seyrek de olsa surekli damlayan olaylar tavani sonsuza uzatamaz.
  void setBusy(bool b) {
    if (b && !_busy) _busySince = millis();
    _busy = b;
  }

  State state() const { return _state; }
  bool  asleep() const { return _state == SLEEP; }

  // Her loop cagrilir. Durumu gunceller ve arka isigi hedefe dogru fade eder.
  // Donus: durum bu tick'te DEGISTIYSE true (main yan etkileri edge'de uygular).
  bool tick() {
    uint32_t now = millis();
    uint32_t idle = now - _lastActivity;

    // Claude mesgulken uyanik tut: emniyet tavanina kadar hep ACTIVE (kisma/uyku
    // yok). Stop gelince (_busy=false) idle sayaci son olaydan baslar -> normal
    // kis/uyku. Tavan asilirsa (Stop kayip) normal idle'a dus.
    // TAVAN busy'nin BASINDAN olculur: eskiden son olaydan olculuyordu ve her yeni
    // olay tavani bastan baslatiyordu -> 10 dk'dan sik damlayan bir olay akisi (ikinci
    // bir oturum, arka plan isi, /loop) cihazi SURESIZ uyanik birakiyordu.
    bool hold = _busy && (now - _busySince < T_BUSY_MAX_MS);

    State next = hold                 ? ACTIVE
               : (idle >= T_SLEEP_MS) ? SLEEP
               : (idle >= T_DIM_MS)   ? DIM
                                      : ACTIVE;
    bool changed = (next != _state);
    _state = next;
    _target = (next == SLEEP) ? BL_OFF : (next == DIM) ? BL_DIM : BL_FULL;

    // yumusak, bloklamayan fade
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
  bool     _busy         = false;    // Claude calisiyor mu (Stop'a kadar)
  uint32_t _busySince    = 0;        // _busy false->true kenari (emniyet tavaninin sifir noktasi)
  uint32_t _lastActivity = 0;
  uint32_t _lastRamp     = 0;
  uint16_t _cur          = BL_FULL;   // 0..255, ara degerler icin 16-bit
  uint16_t _target       = BL_FULL;
};
