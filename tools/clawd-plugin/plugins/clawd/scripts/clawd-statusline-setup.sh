#!/usr/bin/env bash
# clawd statusLine KURULUM — Claude Code'un statusLine'ini clawd wrapper'ina baglar.
# Wrapper senin MEVCUT statusLine'ini AYNEN calistirir (CLI gorunumu degismez) +
# ayrica cihaza `POST /status` gonderir (device HUD ust bandi = model/ctx/kota).
#
# Idempotent (tekrar calistirmak zararsiz). Geri almak: --uninstall.
#
#   bash clawd-statusline-setup.sh              # kur
#   bash clawd-statusline-setup.sh --uninstall  # eski statusLine'a don
#
# Neden gerekli: Claude Code plugin'lere ANA statusLine'i manifest'ten VERDIRMEZ
# (yalniz agent/subagentStatusLine). Tek desteklenen yol: settings.json'daki
# statusLine.command'i plugin'in wrapper'ina yonlendirmek -> bu script onu yapar.
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WRAPPER="$DIR/clawd-statusline.sh"
SETTINGS="${CLAUDE_SETTINGS:-$HOME/.claude/settings.json}"
INNERFILE="$HOME/.claude/.clawd-statusline-inner"
MARK="clawd-statusline.sh"

command -v jq >/dev/null 2>&1 || { echo "HATA: jq gerekli (brew install jq)"; exit 1; }
mkdir -p "$(dirname "$SETTINGS")"
[ -f "$SETTINGS" ] || echo '{}' > "$SETTINGS"

cur="$(jq -r '.statusLine.command // ""' "$SETTINGS" 2>/dev/null || echo "")"
write() { local tmp; tmp="$(mktemp)"; jq "$1" "$SETTINGS" > "$tmp" && mv "$tmp" "$SETTINGS"; }

if [ "${1:-}" = "--uninstall" ]; then
  case "$cur" in
    *"$MARK"*)
      inner="$(cat "$INNERFILE" 2>/dev/null || echo "")"
      if [ -n "$inner" ]; then
        write ".statusLine = {type:\"command\", command:\"$(printf '%s' "$inner" | sed 's/\\/\\\\/g; s/"/\\"/g')\"}"
        echo "geri alindi: statusLine eski komuta dondu."
      else
        write 'del(.statusLine)'
        echo "geri alindi: statusLine kaldirildi (onceden yoktu)."
      fi
      rm -f "$INNERFILE"
      ;;
    *) echo "clawd statusLine kurulu degil, yapacak sey yok." ;;
  esac
  exit 0
fi

# --- kurulum ---
case "$cur" in
  *"$MARK"*) echo "zaten kurulu (statusLine clawd wrapper'ina bagli). Degisiklik yok."; exit 0 ;;
esac

# Mevcut komutu 'inner' olarak sakla (wrapper onu aynen calistirir).
printf '%s' "$cur" > "$INNERFILE"
newcmd="bash '$WRAPPER'"
write ".statusLine = {type:\"command\", command:\"$(printf '%s' "$newcmd" | sed 's/\\/\\\\/g; s/"/\\"/g')\"}"

echo "kuruldu."
[ -n "$cur" ] && echo "  mevcut statusLine korundu (wrapper icinden calisir)." \
              || echo "  onceden statusLine yoktu; cihaz yansimasi aktif, CLI'da ekstra satir yok."
echo "  cihaz adresi: ${CLAWD_HOST:-clawd.local} (farkliysa .claude/settings.local.json env: CLAWD_HOST)."
echo "  geri almak: bash '$DIR/clawd-statusline-setup.sh' --uninstall"
