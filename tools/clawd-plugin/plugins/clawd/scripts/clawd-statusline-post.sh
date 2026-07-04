#!/usr/bin/env bash
# clawd statusLine -> cihaz koprusu (SIDE-EFFECT; statusLine ciktisi URETMEZ).
#
# Claude Code statusLine komutunun stdin'inde verilen zengin JSON'u alir
# (.model.display_name, .context_window.used_percentage, .rate_limits.*), ozetler
# ve DEGISTIYSE cihaza `POST /status` atar (fire-and-forget). Cihaz ust bandinda
# `Model ctx:% 5h:% wk:%` (yuzdeler kullanima gore renkli) gosterir.
#
# NEDEN AYRI/THROTTLE: statusLine cok sik calisir; yalniz ozet DEGISINCE gonderilir
# (son deger tmp dosyada) -> cihazi/agi bosuna mesgul etmez. Firmware /status'u GUC
# yonetiminde AKTIVITE saymaz -> bu akis cihazi uyanik tutmaz / uyandirmaz.
#
# RESET SELF-HEAL: cihaz reset olunca HUD "-" placeholder'a doner ama host'un dedup'i
# son degeri tuttugu icin normalde yeniden gonderilmez. Bunun icin cihazi SU AN gosteren
# (sahip) oturum, degeri en fazla ~HEAL_EVERY sn'de bir dedup'a bakmadan yeniden yollar
# -> cihaz kendini onarir. Firmware'de setStatus ayni degeri dedup ettiginden EKRAN
# TITREMEZ; sahip olmayan oturumlar susar -> oturumlar-arasi flip olmaz.
#
# Cihaz adresi: CLAWD_HOST env (varsayilan clawd.local; clawd-hook.sh ile ayni mantik).
set -u

HOST="${CLAWD_HOST:-clawd.local}"
TIMEOUT="${CLAWD_TIMEOUT:-2}"

command -v jq   >/dev/null 2>&1 || exit 0
command -v curl >/dev/null 2>&1 || exit 0

INPUT="$(cat)"
jqr() { printf '%s' "$INPUT" | jq -r "$1" 2>/dev/null; }

# STATE: dedup dosyasi OTURUM BASINA (session_id ile). Es zamanli oturumlar boylece
# birbirinin son-degerini EZMEZ -> her oturum yalniz KENDI degeri degisince gonderir,
# cihaz "son aktif oturum"u stabil gosterir (bosta oturumlar bosuna repost yapmaz).
# session_id yoksa tek paylasimli dosyaya duser (eski davranis; hicbir sey bozulmaz).
sid="$(jqr '.session_id // empty')"; sid="${sid//[^A-Za-z0-9_-]/}"
STATE="${TMPDIR:-/tmp}/clawd-statusline-${sid:-shared}.last"

model="$(jqr '.model.display_name // ""')"
model="${model%% (*}"      # "Opus 4.8 (1M context)" -> "Opus 4.8" (parantezli eki at, cihazda sigsin)
ctx="$(jqr '.context_window.used_percentage // empty')"
h5="$(jqr '.rate_limits.five_hour.used_percentage // empty')"
wk="$(jqr '.rate_limits.seven_day.used_percentage // empty')"

# yuzde -> tam sayi; veri yoksa -1 (cihaz o segmenti gizler)
rnd() { if [ -n "$1" ]; then printf '%.0f' "$1"; else printf '%s' '-1'; fi; }
ctx="$(rnd "$ctx")"; h5="$(rnd "$h5")"; wk="$(rnd "$wk")"

body="$(jq -nc --arg m "$model" --argjson ctx "$ctx" --argjson h5 "$h5" --argjson wk "$wk" \
        '{m:$m, ctx:$ctx, h5:$h5, wk:$wk}' 2>/dev/null)" || exit 0

# fire-and-forget: arka planda, cikti /dev/null -> Claude Code beklemez
post() { ( curl -s -m "$TIMEOUT" -o /dev/null -X POST "http://${HOST}/status" \
             -H 'Content-Type: application/json' -d "$body" >/dev/null 2>&1 & ) ; }

now="$(date +%s 2>/dev/null || echo 0)"
OWNER="${TMPDIR:-/tmp}/clawd-statusline-owner"   # cihazi SU AN gosteren oturum (+zaman): paylasimli
HEAL_EVERY="${CLAWD_HEAL_SECS:-15}"              # sahip oturum en fazla bu periyotla yeniden yollar

# GERCEK degisiklik -> yolla + STATE guncelle + cihazin "sahipligini" bu oturuma al
# (cihaz artik BU oturumun degerini gosteriyor). Boylece yalniz bu oturum tazeleyecek.
if [ ! -f "$STATE" ] || [ "$(cat "$STATE" 2>/dev/null)" != "$body" ]; then
  printf '%s' "$body" > "$STATE" 2>/dev/null || true
  post
  printf '%s %s' "${sid:-shared}" "$now" > "$OWNER" 2>/dev/null || true
  exit 0
fi

# DEGISMEDI. Cihaz reset olduysa placeholder ("-") gosteriyor olabilir; host bunu bilemez.
# COZUM: cihazi su an gosteren (SAHIP) oturum, en fazla HEAL_EVERY sn'de bir son degeri
# YENIDEN yollar -> reset'ten ~HEAL_EVERY sn sonra deger geri gelir. Firmware ayni degeri
# dedup ettiginden bu tekrar EKRANDA TITREME YAPMAZ (yalniz gercekten farkli/reset sonrasi cizer).
# Sahip OLMAYAN (bosta/arka) oturumlar susar -> oturumlar-arasi flip YOK.
owner_line="$(cat "$OWNER" 2>/dev/null)"
owner_sid="${owner_line%% *}"; owner_ts="${owner_line##* }"
case "$owner_ts" in ''|*[!0-9]*) owner_ts=0 ;; esac
if [ "$owner_sid" = "${sid:-shared}" ] && [ "$now" -ge 0 ] 2>/dev/null \
   && [ $(( now - owner_ts )) -ge "$HEAL_EVERY" ]; then
  post
  printf '%s %s' "${sid:-shared}" "$now" > "$OWNER" 2>/dev/null || true
fi
exit 0
