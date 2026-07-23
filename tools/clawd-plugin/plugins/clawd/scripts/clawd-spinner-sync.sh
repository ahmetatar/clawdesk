#!/usr/bin/env bash
# clawd spinner SYNC — mirrors Claude Code's spinner word pool to the device.
# Generates the words via clawd-spinner-extract.sh (a user-specific list if present,
# otherwise a live extraction from the installed CC binary), wraps them in JSON and
# sends `POST /words` to the device.
#
# NOT fire-and-forget: it's a user command -> it gives feedback (count + HTTP status).
# Device address: CLAWD_HOST env (default clawd.local; same logic as the hooks).
#
# Usage:
#   clawd-spinner-sync.sh            # send to the device
#   clawd-spinner-sync.sh --preview  # show the words/count without sending
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXTRACT="$DIR/clawd-spinner-extract.sh"
HOST="${CLAWD_HOST:-clawd.local}"
TIMEOUT="${CLAWD_TIMEOUT:-5}"

command -v jq   >/dev/null 2>&1 || { echo "jq required."   >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "curl required." >&2; exit 1; }

words="$(bash "$EXTRACT")" || { echo "Could not extract words (see the error above)." >&2; exit 1; }
n="$(printf '%s\n' "$words" | grep -c .)"
[ "$n" -gt 0 ] || { echo "Empty word list -> nothing sent." >&2; exit 1; }

if [ "${1:-}" = "--preview" ]; then
  echo "Source: $( [ -s "$HOME/.claude/clawd-spinner-words.txt" ] && echo '~/.claude/clawd-spinner-words.txt (custom)' || echo 'Claude Code binary' )"
  echo "Word count: $n"
  printf '%s\n' "$words" | paste -sd' ' -
  exit 0
fi

# {"w":[...]}  — turn the words into a JSON array (jq -R/-s: lines -> string array)
body="$(printf '%s\n' "$words" | jq -R . | jq -s '{w: .}')" || { echo "Could not build JSON." >&2; exit 1; }

echo "sending $n spinner words to clawd (http://${HOST}/words)..."
resp="$(curl -s -m "$TIMEOUT" -w $'\n%{http_code}' -X POST "http://${HOST}/words" \
        -H 'Content-Type: application/json' -d "$body" 2>/dev/null)" || true
code="$(printf '%s' "$resp" | tail -n1)"
payload="$(printf '%s' "$resp" | sed '$d')"

case "$code" in
  200) echo "OK ($n words active). Device response: ${payload:-<empty>}" ;;
  "")  echo "Could not reach the device (timeout/network). Is CLAWD_HOST=${HOST} correct and the device on?" >&2; exit 1 ;;
  *)   echo "Device returned an error: HTTP $code ${payload:-}" >&2; exit 1 ;;
esac
