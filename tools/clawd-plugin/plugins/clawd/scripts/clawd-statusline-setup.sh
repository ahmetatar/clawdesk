#!/usr/bin/env bash
# clawd statusLine INSTALL — wires Claude Code's statusLine to the clawd wrapper.
# The wrapper runs your EXISTING statusLine verbatim (the CLI look is unchanged) +
# also sends `POST /status` to the device (device HUD top band = model/ctx/quota).
#
# Idempotent (safe to re-run). To revert: --uninstall.
#
#   bash clawd-statusline-setup.sh              # install
#   bash clawd-statusline-setup.sh --uninstall  # restore the old statusLine
#
# Why it's needed: Claude Code does NOT let plugins set the MAIN statusLine from the
# manifest (only agent/subagentStatusLine). The only supported path: point
# statusLine.command in settings.json at the plugin's wrapper -> this script does that.
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WRAPPER="$DIR/clawd-statusline.sh"
SETTINGS="${CLAUDE_SETTINGS:-$HOME/.claude/settings.json}"
INNERFILE="$HOME/.claude/.clawd-statusline-inner"
MARK="clawd-statusline.sh"

command -v jq >/dev/null 2>&1 || { echo "ERROR: jq required (brew install jq)"; exit 1; }
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
        echo "reverted: statusLine restored to the previous command."
      else
        write 'del(.statusLine)'
        echo "reverted: statusLine removed (there was none before)."
      fi
      rm -f "$INNERFILE"
      ;;
    *) echo "clawd statusLine is not installed, nothing to do." ;;
  esac
  exit 0
fi

# --- install ---
case "$cur" in
  *"$MARK"*) echo "already installed (statusLine wired to the clawd wrapper). No change."; exit 0 ;;
esac

# Save the current command as 'inner' (the wrapper runs it verbatim).
printf '%s' "$cur" > "$INNERFILE"
newcmd="bash '$WRAPPER'"
write ".statusLine = {type:\"command\", command:\"$(printf '%s' "$newcmd" | sed 's/\\/\\\\/g; s/"/\\"/g')\"}"

echo "installed."
[ -n "$cur" ] && echo "  your existing statusLine is preserved (runs inside the wrapper)." \
              || echo "  there was no statusLine before; device mirroring is active, no extra line in the CLI."
echo "  device address: ${CLAWD_HOST:-clawd.local} (if different, set env CLAWD_HOST in .claude/settings.local.json)."
echo "  to revert: bash '$DIR/clawd-statusline-setup.sh' --uninstall"
