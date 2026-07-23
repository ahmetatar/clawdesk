#!/usr/bin/env bash
# clawd UNINSTALL — reverts all the PC-side integrations install.sh made: the global
# plugin, the statusLine bridge, the CLAWD_HOST env, and (optionally) the static IP and
# WiFi password. It does NOT touch the device's physical firmware (no USB needed); the
# device simply goes quiet once it's unplugged / unused.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_SCRIPTS="$ROOT/tools/clawd-plugin/plugins/clawd/scripts"
GLOBAL_SETTINGS="$HOME/.claude/settings.json"
CONFIG_H="$ROOT/include/config.h"
SECRETS="$ROOT/include/secrets.h"

say()  { printf '\n\033[1m%s\033[0m\n' "$1"; }
info() { printf '  %s\n' "$1"; }
warn() { printf '  \033[33m%s\033[0m\n' "$1" >&2; }
ask_yn() {
  local prompt="$1" def="$2" reply
  read -r -p "  $prompt " reply
  reply="${reply:-$def}"
  case "$reply" in y|Y|yes|Yes) return 0 ;; *) return 1 ;; esac
}

say "clawd uninstall"
info "Reverts ALL settings install.sh made on this machine."

# ---- 1) plugin (global) ----
say "1/5 - plugin"
if command -v claude >/dev/null 2>&1; then
  if claude plugin list 2>/dev/null | grep -q 'clawd@clawd'; then
    claude plugin uninstall clawd@clawd --scope user -y && info "plugin uninstalled (scope: user)."
  else
    info "plugin is not installed."
  fi
  claude plugin marketplace remove clawd >/dev/null 2>&1 && info "local marketplace removed."
else
  warn "'claude' CLI not found, plugin step skipped."
fi

# ---- 2) statusLine ----
say "2/5 - statusLine"
bash "$PLUGIN_SCRIPTS/clawd-statusline-setup.sh" --uninstall

# ---- 3) CLAWD_HOST (global settings) ----
say "3/5 - device address"
if [ -f "$GLOBAL_SETTINGS" ] && command -v jq >/dev/null 2>&1; then
  cur="$(jq -r '.env.CLAWD_HOST // ""' "$GLOBAL_SETTINGS" 2>/dev/null)"
  if [ -n "$cur" ]; then
    tmp="$(mktemp)"
    jq 'del(.env.CLAWD_HOST) | if (.env // {} | length) == 0 then del(.env) else . end' "$GLOBAL_SETTINGS" > "$tmp" && mv "$tmp" "$GLOBAL_SETTINGS"
    info "removed: ~/.claude/settings.json env.CLAWD_HOST (old value: $cur)"
  else
    info "CLAWD_HOST was not set."
  fi
fi

# ---- 4) static IP ----
say "4/5 - static IP"
cur_static="$(sed -n 's/#define CLAWD_STATIC_IP \(.*\)/\1/p' "$CONFIG_H" 2>/dev/null | head -1)"
if [ "$cur_static" = "1" ]; then
  sed -i '' -e "s/#define CLAWD_STATIC_IP .*/#define CLAWD_STATIC_IP 0/" "$CONFIG_H"
  info "static IP disabled: include/config.h will now use DHCP (does NOT restore the old IP, only turns it off)."
else
  info "static IP is already off."
fi

# ---- 5) secrets.h ----
say "5/5 - WiFi password"
if [ -f "$SECRETS" ]; then
  if ask_yn "Delete include/secrets.h (contains your WiFi password)? (y/N)" "N"; then
    rm -f "$SECRETS"
    info "deleted: include/secrets.h"
  else
    info "kept: include/secrets.h (already in .gitignore, never enters the repo)"
  fi
fi

say "done"
info "clawd no longer connects to any Claude Code session on this machine."
info "The device keeps running its existing firmware (unplug it / flash a blank firmware over USB -> full reset)."
info "To reinstall: ./install.sh"
