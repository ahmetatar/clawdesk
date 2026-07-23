#!/usr/bin/env bash
# clawd INSTALL — takes the device from the box to working. Asks a few questions and
# does the rest itself: WiFi is written into the firmware + the device is flashed, and
# on the PC side the clawd plugin is installed GLOBALLY (active in every project), the
# statusLine HUD bridge is wired, and the spinner word pool is synced.
#
# Idempotent: safe to re-run (asks about existing settings, leaves anything you don't
# want to change as-is). To revert: ./uninstall.sh
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$ROOT/tools/clawd-plugin"
PLUGIN_SCRIPTS="$PLUGIN_DIR/plugins/clawd/scripts"
GLOBAL_SETTINGS="$HOME/.claude/settings.json"

say()  { printf '\n\033[1m%s\033[0m\n' "$1"; }
info() { printf '  %s\n' "$1"; }
warn() { printf '  \033[33m%s\033[0m\n' "$1" >&2; }
err()  { printf '  \033[31m%s\033[0m\n' "$1" >&2; }
ask()  { local prompt="$1" def="$2" reply; read -r -p "  $prompt " reply; echo "${reply:-$def}"; }
ask_yn() {
  local prompt="$1" def="$2" reply
  read -r -p "  $prompt " reply
  reply="${reply:-$def}"
  case "$reply" in y|Y|yes|Yes) return 0 ;; *) return 1 ;; esac
}
# An IP that answers ping = in use. macOS ping: -c1 one packet, -t1 1s timeout.
ip_in_use() { ping -c 1 -t 1 "$1" >/dev/null 2>&1; }

say "clawd install"
info "The only thing it asks: WiFi name + password. It handles the rest -> the static IP/"
info "gateway/mask/DNS are auto-detected from this network, written to the device, the device"
info "is flashed, and on the PC the clawd plugin is installed GLOBALLY (active in every project)"
info "+ the statusLine HUD bridge is wired + the spinner words are synced."
info "Flash/plugin/statusLine/spinner steps are skippable; WiFi + static IP are required."

# ---- 0) prerequisites ----
say "1/5 - prerequisites"
missing=()
command -v pio  >/dev/null 2>&1 || missing+=(platformio)
command -v jq   >/dev/null 2>&1 || missing+=(jq)
command -v curl >/dev/null 2>&1 || missing+=(curl)
command -v claude >/dev/null 2>&1 || missing+=(claude-code)

if [ "${#missing[@]}" -gt 0 ]; then
  err "Missing tools: ${missing[*]}"
  for m in "${missing[@]}"; do
    case "$m" in
      platformio) info "  install: brew install platformio  (or: pipx install platformio)" ;;
      jq)         info "  install: brew install jq" ;;
      curl)       info "  ships with macOS; check your PATH." ;;
      claude-code) info "  install: https://claude.com/claude-code" ;;
    esac
  done
  if command -v brew >/dev/null 2>&1 && ask_yn "Install the missing tools now with Homebrew? (y/N)" "N"; then
    for m in "${missing[@]}"; do
      case "$m" in
        platformio) brew install platformio ;;
        jq)         brew install jq ;;
      esac
    done
  else
    err "Install the missing tools first, then re-run ./install.sh."
    exit 1
  fi
fi
info "ok: pio, jq, curl, claude are ready."

# ---- 1) WiFi credentials ----
say "2/5 - WiFi"
SECRETS="$ROOT/include/secrets.h"
cur_ssid=""
[ -f "$SECRETS" ] && cur_ssid="$(sed -n 's/.*WIFI_SSID "\(.*\)"/\1/p' "$SECRETS")"

do_wifi=1
if [ -n "$cur_ssid" ]; then
  info "current saved SSID: $cur_ssid"
  ask_yn "Do you want to change the WiFi credentials? (y/N)" "N" || do_wifi=0
fi

if [ "$do_wifi" = "1" ]; then
  ssid="$(ask "WiFi name (SSID) [2.4GHz]:" "")"
  while [ -z "$ssid" ]; do ssid="$(ask "SSID cannot be empty, enter again:" "")"; done
  pass=""
  read -r -s -p "  WiFi password: " pass; echo
  mkdir -p "$ROOT/include"
  {
    echo "#pragma once"
    echo "// Written by install.sh. This file is in .gitignore -> the password never enters the repo."
    echo "#define WIFI_SSID \"$ssid\""
    echo "#define WIFI_PASS \"$pass\""
  } > "$SECRETS"
  info "written: include/secrets.h"
fi

# ---- 1b) static IP (REQUIRED — event delivery must not lag/drop on mDNS/DHCP changes) ----
CONFIG_H="$ROOT/include/config.h"

active_if="$(route -n get default 2>/dev/null | awk '/interface:/{print $2}')"
det_gw=""; det_mask=""
if [ -n "$active_if" ]; then
  det_gw="$(ipconfig getoption "$active_if" router 2>/dev/null)"
  det_mask="$(ipconfig getoption "$active_if" subnet_mask 2>/dev/null)"
fi
det_gw="${det_gw:-192.168.1.1}"
det_mask="${det_mask:-255.255.255.0}"

# gateway/mask auto-detected; also auto-pick the device IP (a host from the top of the
# range -> DHCP pools usually hand out from the bottom, so no collision).
suggested_ip=""
if command -v python3 >/dev/null 2>&1; then
  suggested_ip="$(python3 - "$det_gw" "$det_mask" <<'PYEOF'
import ipaddress, sys
gw, mask = sys.argv[1], sys.argv[2]
try:
    net = ipaddress.ip_network(f"{gw}/{mask}", strict=False)
    hosts = list(net.hosts())
    cand = hosts[-1] if hosts else None
    print(cand if cand and str(cand) != gw else "")
except Exception:
    print("")
PYEOF
)"
fi
# no python3: assume /24, set gateway's last octet to .250 (if it clashes with gw, .249)
if [ -z "$suggested_ip" ]; then
  gw_prefix="$(printf '%s' "$det_gw" | awk -F. '{printf "%s.%s.%s",$1,$2,$3}')"
  suggested_ip="$gw_prefix.250"
  [ "$suggested_ip" = "$det_gw" ] && suggested_ip="$gw_prefix.249"
fi

gw="$det_gw"
mask="$det_mask"
dns="$det_gw"
gw_prefix="$(printf '%s' "$det_gw" | awk -F. '{printf "%s.%s.%s",$1,$2,$3}')"

# if the suggested IP answers ping (in use), decrement the last octet until a free one.
if [ -n "$suggested_ip" ] && ip_in_use "$suggested_ip"; then
  info "suggested IP $suggested_ip is in use, searching for a free address..."
  last_oct="$(printf '%s' "$suggested_ip" | awk -F. '{print $4}')"
  free_ip=""
  for _ in $(seq 1 20); do
    last_oct=$((last_oct - 1))
    [ "$last_oct" -lt 2 ] && break
    cand="$gw_prefix.$last_oct"
    [ "$cand" = "$det_gw" ] && continue
    if ! ip_in_use "$cand"; then free_ip="$cand"; break; fi
  done
  [ -n "$free_ip" ] && suggested_ip="$free_ip" \
    && info "free address found: $suggested_ip" \
    || warn "no free address found nearby, will try $suggested_ip anyway."
fi

# single optional question: leave blank -> the IP the script picked is used.
ip="$(ask "Static IP for the device [$suggested_ip] (Enter -> auto):" "$suggested_ip")"
# if the user typed an IP, ping it too and warn (still used).
if [ "$ip" != "$suggested_ip" ] && ip_in_use "$ip"; then
  warn "WARNING: $ip currently answers ping (another device may be using it)."
fi
info "static IP: $ip  (gateway $gw / mask $mask / DNS $dns)"
if [ -n "$ip" ]; then
  ip_arr="$(printf '%s' "$ip"   | awk -F. '{printf "%s, %s, %s, %s",$1,$2,$3,$4}')"
  gw_arr="$(printf '%s' "$gw"   | awk -F. '{printf "%s, %s, %s, %s",$1,$2,$3,$4}')"
  mask_arr="$(printf '%s' "$mask" | awk -F. '{printf "%s, %s, %s, %s",$1,$2,$3,$4}')"
  dns_arr="$(printf '%s' "$dns" | awk -F. '{printf "%s, %s, %s, %s",$1,$2,$3,$4}')"
  sed -i '' \
    -e "s/#define CLAWD_STATIC_IP .*/#define CLAWD_STATIC_IP 1/" \
    -e "s/constexpr uint8_t IP_LOCAL\[4\][^;]*;/constexpr uint8_t IP_LOCAL[4]   = {$ip_arr};/" \
    -e "s/constexpr uint8_t IP_GATEWAY\[4\][^;]*;/constexpr uint8_t IP_GATEWAY[4] = {$gw_arr};/" \
    -e "s/constexpr uint8_t IP_SUBNET\[4\][^;]*;/constexpr uint8_t IP_SUBNET[4]  = {$mask_arr};/" \
    -e "s/constexpr uint8_t IP_DNS\[4\][^;]*;/constexpr uint8_t IP_DNS[4]     = {$dns_arr};/" \
    "$CONFIG_H"
  detected_host="$ip"
  info "static IP written: $ip"
else
  err "Could not determine an IP (no auto suggestion either) -> enter one manually and re-run."
fi

# ---- 2) flash ----
say "3/5 - flash the device"
detected_host="${detected_host:-}"
if ask_yn "Flash the device over USB now? (Y/n)" "Y"; then
  ports="$(pio device list 2>/dev/null | grep -Eo '/dev/cu\.[A-Za-z0-9_.-]+' | grep -Ev 'Bluetooth-Incoming-Port|debug-console' || true)"
  usb_ports="$(printf '%s\n' "$ports" | grep -Ei 'usbserial|usbmodem|SLAB|wchusbserial' || true)"
  if [ -z "$usb_ports" ]; then
    warn "No USB-serial port found. Connect the device via USB and press Enter."
    read -r
    ports="$(pio device list 2>/dev/null | grep -Eo '/dev/cu\.[A-Za-z0-9_.-]+' | grep -Ev 'Bluetooth-Incoming-Port|debug-console' || true)"
    usb_ports="$(printf '%s\n' "$ports" | grep -Ei 'usbserial|usbmodem|SLAB|wchusbserial' || true)"
  fi

  port=""
  n_ports="$(printf '%s\n' "$usb_ports" | grep -c . || true)"
  if [ "$n_ports" = "1" ]; then
    port="$usb_ports"
  elif [ "$n_ports" -gt 1 ]; then
    info "multiple serial ports found, pick one:"
    select p in $usb_ports; do [ -n "$p" ] && port="$p" && break; done
  fi

  if [ -z "$port" ]; then
    err "No serial port found. Connect the device via USB and re-run ./install.sh."
  else
    info "flashing: $port (this can take 30-60 s)"
    cert="$HOME/.platformio/system-ca-bundle.pem"
    if [ -f "$cert" ]; then
      SSL_CERT_FILE="$cert" REQUESTS_CA_BUNDLE="$cert" pio run -d "$ROOT" -t upload --upload-port "$port"
    else
      pio run -d "$ROOT" -t upload --upload-port "$port"
    fi
    upload_rc=$?
    if [ "$upload_rc" != "0" ]; then
      err "Flash failed. Check the cable/port and try again."
    else
      info "flash done. reading serial output to get the device IP (WiFi join can take ~20 s)..."
      logf="$(mktemp)"
      ( pio device monitor -d "$ROOT" --port "$port" --baud 115200 --quiet > "$logf" 2>&1 ) &
      monpid=$!
      # Poll the log until the device prints its IP or reports a WiFi failure (up to 25 s).
      # Fixed sleeps missed the IP when WiFi joined slowly -> we now wait for the real marker.
      ip_line=""; wifi_fail=0
      for _ in $(seq 1 25); do
        sleep 1
        ip_line="$(grep -Eo 'IP: [0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' "$logf" | head -1 | awk '{print $2}')"
        [ -n "$ip_line" ] && break
        if grep -q 'WiFi connect failed' "$logf"; then wifi_fail=1; break; fi
      done
      kill "$monpid" >/dev/null 2>&1
      wait "$monpid" 2>/dev/null
      # matches the firmware serial marker (see main.cpp connectWiFi failure path).
      if [ "$wifi_fail" = "1" ]; then
        err "The device could not connect to WiFi. Check the SSID/password (2.4GHz only)."
        err "Fix them by re-running ./install.sh (it will re-flash)."
      elif [ -n "$ip_line" ]; then
        detected_host="$ip_line"          # the REAL IP the device got (overrides the assumed static)
        info "device connected to WiFi. IP: $ip_line"
      else
        warn "Could not read the IP from serial; using the assigned static IP: ${detected_host:-clawd.local}."
      fi
      rm -f "$logf"
    fi
  fi
else
  info "flash skipped."
fi

# ---- 3) PC side: CLAWD_HOST (global) ----
say "4/5 - device address (global)"
# Use the auto-detected/assigned IP directly — no question asked.
if [ -n "$detected_host" ]; then
  host="$detected_host"
  info "device address (auto): $host"
else
  # Only ask as a last resort, if we truly couldn't determine an IP.
  host="$(ask "Device IP could not be detected — enter it (blank -> clawd.local/mDNS):" "clawd.local")"
fi
mkdir -p "$HOME/.claude"
[ -f "$GLOBAL_SETTINGS" ] || echo '{}' > "$GLOBAL_SETTINGS"
tmp="$(mktemp)"
jq --arg h "$host" '.env = ((.env // {}) + {CLAWD_HOST: $h})' "$GLOBAL_SETTINGS" > "$tmp" && mv "$tmp" "$GLOBAL_SETTINGS"
info "written: ~/.claude/settings.json  env.CLAWD_HOST=$host  (applies to all projects)"

# Wait for the device to actually be reachable BEFORE the first communication (word sync).
# It just booted and is joining WiFi; firing POST /words too early was the "sync failed" cause.
device_online=0
info "waiting for the device to come online at http://$host ..."
for _ in $(seq 1 30); do
  if curl -s -m 2 "http://${host}/health" >/dev/null 2>&1; then device_online=1; break; fi
  sleep 1
done
if [ "$device_online" = "1" ]; then
  info "device is online."
else
  warn "device not reachable at $host yet (may still be booting / wrong network)."
fi

# ---- 4) plugin + statusLine + spinner (GLOBAL, user scope) — done automatically, no prompts ----
say "5/5 - Claude Code integration (global)"
claude plugin marketplace add "$PLUGIN_DIR" >/dev/null 2>&1 || claude plugin marketplace update clawd >/dev/null 2>&1
if claude plugin install clawd@clawd --scope user; then
  info "plugin installed (scope: user)."
else
  warn "plugin install failed / may already be installed. Check with 'claude plugin list'."
fi

# statusLine bridge is a local settings.json edit — always wire it.
bash "$PLUGIN_SCRIPTS/clawd-statusline-setup.sh"

# Word sync needs the device; only attempt it if we confirmed it's online above.
if [ "$device_online" = "1" ]; then
  CLAWD_HOST="$host" bash "$PLUGIN_SCRIPTS/clawd-spinner-sync.sh" || warn "spinner sync failed (device online but rejected the request?)."
else
  warn "skipped spinner sync — device offline. Once it's online, run: /clawd:sync-spinner-words"
fi

say "done"
info "test:  curl http://$host/health"
info "Hooks take effect in a NEW Claude Code session (close/reopen the current one)."
info "to revert: ./uninstall.sh"
