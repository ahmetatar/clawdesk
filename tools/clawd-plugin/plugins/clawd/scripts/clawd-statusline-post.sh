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
# Cihaz adresi: CLAWD_HOST env (varsayilan clawd.local; clawd-hook.sh ile ayni mantik).
set -u

HOST="${CLAWD_HOST:-clawd.local}"
TIMEOUT="${CLAWD_TIMEOUT:-2}"
STATE="${TMPDIR:-/tmp}/clawd-statusline.last"

command -v jq   >/dev/null 2>&1 || exit 0
command -v curl >/dev/null 2>&1 || exit 0

INPUT="$(cat)"
jqr() { printf '%s' "$INPUT" | jq -r "$1" 2>/dev/null; }

model="$(jqr '.model.display_name // ""')"
ctx="$(jqr '.context_window.used_percentage // empty')"
h5="$(jqr '.rate_limits.five_hour.used_percentage // empty')"
wk="$(jqr '.rate_limits.seven_day.used_percentage // empty')"

# yuzde -> tam sayi; veri yoksa -1 (cihaz o segmenti gizler)
rnd() { if [ -n "$1" ]; then printf '%.0f' "$1"; else printf '%s' '-1'; fi; }
ctx="$(rnd "$ctx")"; h5="$(rnd "$h5")"; wk="$(rnd "$wk")"

body="$(jq -nc --arg m "$model" --argjson ctx "$ctx" --argjson h5 "$h5" --argjson wk "$wk" \
        '{m:$m, ctx:$ctx, h5:$h5, wk:$wk}' 2>/dev/null)" || exit 0

# degismediyse gonderme (spam/gereksiz SPI onleme)
[ -f "$STATE" ] && [ "$(cat "$STATE" 2>/dev/null)" = "$body" ] && exit 0
printf '%s' "$body" > "$STATE" 2>/dev/null || true

# fire-and-forget: arka planda, cikti /dev/null -> Claude Code beklemez
( curl -s -m "$TIMEOUT" -o /dev/null -X POST "http://${HOST}/status" \
       -H 'Content-Type: application/json' -d "$body" >/dev/null 2>&1 & )
exit 0
