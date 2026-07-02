#!/usr/bin/env bash
# clawd statusLine WRAPPER — Claude Code'un statusLine.command'i buna yonlendirilir
# (clawd-statusline-setup.sh yapar). Iki is yapar, stdin JSON'u ikisine de verir:
#   1) cihaza yansit: clawd-statusline-post.sh (arka planda, ASLA bloklamaz)
#   2) KULLANICININ ORIJINAL statusLine komutunu aynen calistir -> CLI ciktisi DEGISMEZ
#
# Orijinal ("inner") komut, setup tarafindan $HOME/.claude/.clawd-statusline-inner
# dosyasina yazilir (tirnak/escape derdi olmasin diye arg yerine dosya). Bos ise
# (kullanicinin onceden statusLine'i yoktu) yalniz cihaza yansitilir, CLI'da bos satir.
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INNERFILE="$HOME/.claude/.clawd-statusline-inner"
INPUT="$(cat)"

# 1) cihaza yansit (side-effect; bloklamaz, hata yutulur)
printf '%s' "$INPUT" | bash "$DIR/clawd-statusline-post.sh" >/dev/null 2>&1 || true

# 2) orijinal statusLine ciktisini uret (stdin yeniden verilir)
if [ -f "$INNERFILE" ]; then
  inner="$(cat "$INNERFILE" 2>/dev/null || true)"
  [ -n "$inner" ] && printf '%s' "$INPUT" | eval "$inner"
fi
