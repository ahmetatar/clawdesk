#!/usr/bin/env bash
# clawd statusLine -> device bridge (SIDE-EFFECT; produces NO statusLine output).
#
# Takes the rich JSON given on the stdin of the Claude Code statusLine command
# (.model.display_name, .context_window.used_percentage, .rate_limits.*), summarizes it,
# and IF IT CHANGED sends `POST /status` to the device (fire-and-forget). On the device's
# bottom band it shows `Model ctx:% 5h:% wk:%` (percentages colored by usage) + reset
# countdown `5h: (2h32m) wk: (2d5h)` (gray). The countdown is computed here, on the host
# (rate_limits.*.resets_at epoch) -> the device has no clock, it just draws the ready string.
#
# WHY SEPARATE/THROTTLE: statusLine runs very often; it is sent only WHEN the summary
# CHANGES (last value in a tmp file) -> it doesn't needlessly busy the device/network. The
# firmware does NOT count /status as ACTIVITY in POWER management -> this flow does not keep
# the device awake / wake it.
#
# RESET SELF-HEAL: when the device resets, the HUD returns to the "-" placeholder, but since
# the host's dedup holds the last value it normally won't resend. For this, the session that
# CURRENTLY shows the device (the owner) resends the value at most ~every HEAL_EVERY sec,
# ignoring dedup -> the device heals itself. Since setStatus in the firmware dedups the same
# value, the SCREEN DOES NOT FLICKER; non-owner sessions stay quiet -> no cross-session flip.
#
# Device address: CLAWD_HOST env (default clawd.local; same logic as clawd-hook.sh).
set -u

HOST="${CLAWD_HOST:-clawd.local}"
TIMEOUT="${CLAWD_TIMEOUT:-2}"

command -v jq   >/dev/null 2>&1 || exit 0
command -v curl >/dev/null 2>&1 || exit 0

INPUT="$(cat)"
jqr() { printf '%s' "$INPUT" | jq -r "$1" 2>/dev/null; }

# STATE: dedup file PER SESSION (with session_id). This way concurrent sessions DON'T
# OVERWRITE each other's last-value -> each session sends only when ITS OWN value changes,
# the device stably shows the "last active session" (idle sessions don't needlessly repost).
# If there is no session_id it falls back to a single shared file (old behavior; nothing breaks).
sid="$(jqr '.session_id // empty')"; sid="${sid//[^A-Za-z0-9_-]/}"
STATE="${TMPDIR:-/tmp}/clawd-statusline-${sid:-shared}.last"

model="$(jqr '.model.display_name // ""')"
model="${model%% (*}"      # "Opus 4.8 (1M context)" -> "Opus 4.8" (drop the parenthesized suffix so it fits on the device)
ctx="$(jqr '.context_window.used_percentage // empty')"
h5="$(jqr '.rate_limits.five_hour.used_percentage // empty')"
wk="$(jqr '.rate_limits.seven_day.used_percentage // empty')"
five_reset="$(jqr '.rate_limits.five_hour.resets_at // empty')"
week_reset="$(jqr '.rate_limits.seven_day.resets_at // empty')"

# percentage -> integer; if no data -1 (the device hides that segment)
rnd() { if [ -n "$1" ]; then printf '%.0f' "$1"; else printf '%s' '-1'; fi; }
ctx="$(rnd "$ctx")"; h5="$(rnd "$h5")"; wk="$(rnd "$wk")"

# epoch -> "2h32m" / "2d5h" / "45m" (SAME format as time_until in statusline-command.sh).
# if no resets_at then empty -> the device shows the "-" placeholder.
time_until() {
  [ -z "$1" ] && return
  local secs=$(( $1 - $(date +%s) ))
  [ "$secs" -le 0 ] && { printf 'now'; return; }
  local d=$(( secs / 86400 )) h=$(( (secs % 86400) / 3600 )) m=$(( (secs % 3600) / 60 ))
  if [ "$d" -gt 0 ]; then printf '%dd%dh' "$d" "$h"
  elif [ "$h" -gt 0 ]; then printf '%dh%02dm' "$h" "$m"
  else printf '%dm' "$m"; fi
}
h5r="$(time_until "$five_reset")"
wkr="$(time_until "$week_reset")"

body="$(jq -nc --arg m "$model" --argjson ctx "$ctx" --argjson h5 "$h5" --argjson wk "$wk" \
        --arg h5r "$h5r" --arg wkr "$wkr" \
        '{m:$m, ctx:$ctx, h5:$h5, wk:$wk, h5r:$h5r, wkr:$wkr}' 2>/dev/null)" || exit 0

# fire-and-forget: in the background, output to /dev/null -> Claude Code does not wait
post() { ( curl -s -m "$TIMEOUT" -o /dev/null -X POST "http://${HOST}/status" \
             -H 'Content-Type: application/json' -d "$body" >/dev/null 2>&1 & ) ; }

now="$(date +%s 2>/dev/null || echo 0)"
OWNER="${TMPDIR:-/tmp}/clawd-statusline-owner"   # session that CURRENTLY shows the device (+time): shared
HEAL_EVERY="${CLAWD_HEAL_SECS:-15}"              # the owner session resends at most this often

# REAL change -> send + update STATE + take "ownership" of the device for this session
# (the device now shows THIS session's value). This way only this session will refresh.
if [ ! -f "$STATE" ] || [ "$(cat "$STATE" 2>/dev/null)" != "$body" ]; then
  printf '%s' "$body" > "$STATE" 2>/dev/null || true
  post
  printf '%s %s' "${sid:-shared}" "$now" > "$OWNER" 2>/dev/null || true
  exit 0
fi

# UNCHANGED. If the device reset it may be showing the placeholder ("-"); the host can't know.
# SOLUTION: the session that currently shows the device (the OWNER) RESENDS the last value at
# most every HEAL_EVERY sec -> ~HEAL_EVERY sec after a reset the value comes back. Since the
# firmware dedups the same value, this repeat DOES NOT FLICKER THE SCREEN (it only draws when
# truly different / after a reset). NON-owner (idle/background) sessions stay quiet -> NO cross-session flip.
owner_line="$(cat "$OWNER" 2>/dev/null)"
owner_sid="${owner_line%% *}"; owner_ts="${owner_line##* }"
case "$owner_ts" in ''|*[!0-9]*) owner_ts=0 ;; esac
if [ "$owner_sid" = "${sid:-shared}" ] && [ "$now" -ge 0 ] 2>/dev/null \
   && [ $(( now - owner_ts )) -ge "$HEAL_EVERY" ]; then
  post
  printf '%s %s' "${sid:-shared}" "$now" > "$OWNER" 2>/dev/null || true
fi
exit 0
