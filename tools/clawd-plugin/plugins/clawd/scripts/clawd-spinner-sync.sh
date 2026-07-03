#!/usr/bin/env bash
# clawd spinner SYNC — Claude Code'un spinner kelime havuzunu cihaza yansitir.
# clawd-spinner-extract.sh ile kelimeleri uretir (kullanici ozeli varsa o, yoksa
# kurulu CC ikilisinden canli cikarim), JSON'a sarar ve cihaza `POST /words` atar.
#
# Fire-and-forget DEGIL: kullanici komutu -> geri bildirim verir (adet + HTTP durumu).
# Cihaz adresi: CLAWD_HOST env (varsayilan clawd.local; hook'larla ayni mantik).
#
# Kullanim:
#   clawd-spinner-sync.sh            # cihaza gonder
#   clawd-spinner-sync.sh --preview  # gondermeden kelimeleri/adedi goster
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXTRACT="$DIR/clawd-spinner-extract.sh"
HOST="${CLAWD_HOST:-clawd.local}"
TIMEOUT="${CLAWD_TIMEOUT:-5}"

command -v jq   >/dev/null 2>&1 || { echo "jq gerekli."   >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "curl gerekli." >&2; exit 1; }

words="$(bash "$EXTRACT")" || { echo "Kelime cikarilamadi (yukaridaki hataya bak)." >&2; exit 1; }
n="$(printf '%s\n' "$words" | grep -c .)"
[ "$n" -gt 0 ] || { echo "Bos kelime listesi -> gonderilmedi." >&2; exit 1; }

if [ "${1:-}" = "--preview" ]; then
  echo "Kaynak: $( [ -s "$HOME/.claude/clawd-spinner-words.txt" ] && echo '~/.claude/clawd-spinner-words.txt (ozel)' || echo 'Claude Code ikilisi' )"
  echo "Kelime sayisi: $n"
  printf '%s\n' "$words" | paste -sd' ' -
  exit 0
fi

# {"w":[...]}  — kelimeleri JSON dizisine cevir (jq -R/-s: satirlari string array yap)
body="$(printf '%s\n' "$words" | jq -R . | jq -s '{w: .}')" || { echo "JSON uretilemedi." >&2; exit 1; }

echo "clawd'a $n spinner kelimesi gonderiliyor (http://${HOST}/words)..."
resp="$(curl -s -m "$TIMEOUT" -w $'\n%{http_code}' -X POST "http://${HOST}/words" \
        -H 'Content-Type: application/json' -d "$body" 2>/dev/null)" || true
code="$(printf '%s' "$resp" | tail -n1)"
payload="$(printf '%s' "$resp" | sed '$d')"

case "$code" in
  200) echo "OK ($n kelime aktif). Cihaz yaniti: ${payload:-<bos>}" ;;
  "")  echo "Cihaza ulasilamadi (timeout/ag). CLAWD_HOST=${HOST} dogru mu, cihaz acik mi?" >&2; exit 1 ;;
  *)   echo "Cihaz hata dondurdu: HTTP $code ${payload:-}" >&2; exit 1 ;;
esac
