# clawd — the Claude Code plugin

Mirrors Claude Code hook events to the **clawd device** (CYD/ESP32) via `POST /e`.
The mascot thinks while you write a prompt, "hacks" while a tool runs, celebrates a
commit, looks sorry on an error, and rests when the work is done.

The firmware side (`src/main.cpp` → `mapEvent`) already handles these events; this
plugin is the missing bridge on the PC side. Protocol: `clawd-device-protocol.md`
at the repo root.

---

## The easy way: `./install.sh`

Run `./install.sh` at the repo root — it asks for your WiFi, flashes the device,
installs the plugin **globally** on this machine (`--scope user`, active in every
project), and wires up the statusLine and spinner bridges. `./uninstall.sh` reverts
it. The manual steps below are the same thing done by hand.

## Requirements

1. **A working clawd device** — firmware flashed onto the CYD and connected to
   **your WiFi**. (Setup: the root `README.md` plus your SSID/password in
   `include/secrets.h`; `pio run -t upload`.) The PC and the device must be on the
   **same network**.
2. **`jq` and `curl`** — used by the hook script. curl ships with macOS; jq:
   `brew install jq`.
3. **Claude Code** — the source of the hook events.

Handing the device to someone else takes three things: flash the firmware with
their WiFi once, install the plugin, and note the device's IP if mDNS does not work
on their network. No code edits. `install.sh` does all three.

---

## Manual installation

The plugin lives in the repo as a **local marketplace**. Use the path of **your own
clone** below:

```
# 1) register the marketplace (once; <REPO> = where you cloned it)
/plugin marketplace add <REPO>/tools/clawd-plugin

# 2a) enable GLOBALLY (what install.sh does: active in every project)
/plugin install clawd@clawd --scope user

# 2b) or for this project only (personal, gitignored scope)
/plugin install clawd@clawd --scope local
```

`--scope user` writes to the global settings (`~/.claude/settings.json`,
`enabledPlugins`); `--scope local` writes to the project's
`.claude/settings.local.json`. Hooks take effect in a **new** Claude Code session
(or after `/reload-plugins`).

## Removal

```
claude plugin disable clawd@clawd            # disable temporarily
claude plugin uninstall clawd@clawd -s user  # remove entirely (match the install scope)
```

Or run `./uninstall.sh` at the repo root, which reverts the plugin, the statusLine
and `CLAWD_HOST` in one go.

---

## Device address — where to put your IP

**`clawd.local` (mDNS) is not used.** On some networks and range extenders mDNS is
slow or drops entirely, delaying or breaking event delivery. Instead `install.sh`
always assigns the device a **static IP** (detecting the gateway and subnet of the
network you are on) and uses that IP directly — no DNS delay, nothing to drop.

**1) Find the device's IP** (`install.sh` does this for you; if you need it by hand):
- Serial monitor: the device prints `[clawd] WiFi OK. IP: 192.168.x.y` at boot.
- The router's DHCP client list (hostname: `clawd`).

**2) Set the IP** — `install.sh` writes it to the **global**
`~/.claude/settings.json` (`env.CLAWD_HOST`, valid in every project). You can also
set the same thing per-project in `.claude/settings.local.json` (personal,
gitignored):

```json
{
  "env": {
    "CLAWD_HOST": "192.168.1.200"
  },
  "enabledPlugins": {
    "clawd@clawd": true
  }
}
```

Claude Code injects this `env` into the hook, skipping name resolution entirely.
Each machine keeps its own IP here (the file is not shared).

> **If you change networks** (home ↔ hotspot, etc.): run `./install.sh` again — it
> detects the new gateway/subnet and rewrites the static IP accordingly.
> `./uninstall.sh` does not restore a previous value; it disables the static IP and
> falls back to DHCP.

`CLAWD_TIMEOUT` (seconds, default 2) sets the curl timeout. curl runs **in the
background** with its output going to `/dev/null`, so whatever the timeout is,
Claude Code never waits (fire-and-forget, protocol §6).

---

## Event mapping

| Claude Code hook | sent (`POST /e`) | device animation |
|---|---|---|
| `UserPromptSubmit` | `{"k":"prompt.submit",...}` | **think** |
| `PreToolUse` (all tools) | `{"k":"tool.pre",...}` | **hacking** (repeat = no-op) |
| `PostToolUse` (Bash, git commit/push) | `{"k":"git",...}` | **happy** |
| `PostToolUseFailure` (all tools) | `{"k":"tool.post","d":{"ok":false}}` | **oops** |
| `PreCompact` | `{"k":"compact"}` | **think** |
| `SessionStart` | `{"k":"session.start"}` | **happy** |
| `Stop` | `{"k":"session.stop"}` | **idle** |

**Why there is no "event hell":** the firmware swallows any event that leads to the
animation already showing (`if (id != curAnim) setAnim(...)`). Through a burst of
tool calls clawd stays in a single `hacking` state, and ordinary tool *successes*
produce no packet at all (only git commit/push is celebrated). A turn flows:
**think → hacking → (happy on git) → oops on error → idle.**

---

## Testing (with or without a device)

You can feed the script directly and watch what it does:

```
# no device needed — returns instantly (unreachable host)
echo '{"hook_event_name":"PostToolUseFailure","tool_name":"Bash"}' \
  | CLAWD_HOST=127.0.0.1:9 scripts/clawd-hook.sh

# sends idle to a real device (use your IP)
echo '{"hook_event_name":"Stop"}' \
  | CLAWD_HOST=192.168.1.200 scripts/clawd-hook.sh
```

Liveness check: `curl http://<IP>/health` →
`{"fw":"1.0.0","caps":["anim","led","touch","power","hud","status"]}`.

---

## statusLine → the device's HUD top band

The device's top band shows Claude Code's **statusLine** summary:
`Model  ctx:%  5h:%  wk:%`, with the percentages colored green/yellow/red by usage,
exactly as the statusLine does. The data arrives via `POST /status` and **does not
affect power management** — it neither wakes the device nor keeps it awake.

**Why a separate setup step?** Claude Code does not let plugins set the main
`statusLine` from the manifest (only `agent`/`subagentStatusLine`), and only the
statusLine command can see `context_window` / `rate_limits` data — hooks cannot.
The one supported path is to point `statusLine.command` in `settings.json` at the
plugin's **wrapper**.

**Setup (once):**
```
/clawd:clawd-statusline          # slash command, runs the setup
# or by hand:
bash "<plugin>/scripts/clawd-statusline-setup.sh"
```
The setup **preserves your existing statusLine verbatim** (the wrapper runs it
inside, so the CLI looks unchanged) and adds the device POST alongside it. To
revert: the same script with `--uninstall`. The device address comes from
`CLAWD_HOST` (`install.sh` sets it globally to the static IP).

---

## Spinner words → the device (Claude Code's real list)

The "thinking/working" text on the device's HUD comes from the **real Claude Code
gerund pool** compiled into the firmware (`src/spinner_words.h`, ~178 words —
"Cogitating…", "Herding…", "Combobulating…"). CC does not expose the word currently
on screen to any hook or statusLine interface, so the device cannot mirror it
exactly; instead it picks from **the same pool** (WORK and THINK share one pool;
HAPPY/OOPS stay device-specific, since CC shows no spinner word on success or error).

To push a customised or updated list to the device:

```
/clawd:sync-spinner-words        # slash command: POST /words to the device
# preview first:
bash "<plugin>/scripts/clawd-spinner-sync.sh" --preview
```

- **Source:** `~/.claude/clawd-spinner-words.txt` if it exists (one word per line,
  `#` for comments); otherwise the words are extracted **live** from the installed
  Claude Code binary, independent of version.
- **Persistence:** the list lives in the device's **RAM** and reverts to the
  `spinner_words.h` default on reboot. Re-run the command after a reboot to restore
  a custom list.
- **Refresh the compile-time default** (new CC release):
  `bash "<plugin>/scripts/clawd-spinner-extract.sh" --header > src/spinner_words.h`
  then reflash the firmware.

The device address again comes from `CLAWD_HOST`.

---

## Files

```
tools/clawd-plugin/
  .claude-plugin/marketplace.json      # local marketplace (name: clawd)
  plugins/clawd/
    .claude-plugin/plugin.json         # plugin manifest
    hooks/hooks.json                   # 8 hook events -> clawd-hook.sh
    commands/clawd-statusline.md       # /clawd:clawd-statusline (statusLine setup)
    commands/sync-spinner-words.md     # /clawd:sync-spinner-words (spinner pool -> device)
    scripts/clawd-hook.sh              # event bridge (jq + curl -> POST /e)
    scripts/clawd-statusline-post.sh   # statusLine JSON -> POST /status (throttled)
    scripts/clawd-statusline.sh        # statusLine wrapper (wraps the original + device)
    scripts/clawd-statusline-setup.sh  # points settings.json statusLine at the wrapper
    scripts/clawd-spinner-extract.sh   # extract CC spinner words (--header for spinner_words.h)
    scripts/clawd-spinner-sync.sh      # send the spinner pool to the device (POST /words)
    README.md
```

> **Developer note:** a local-directory install **copies** the plugin into
> `~/.claude/plugins/cache/`. Editing `tools/clawd-plugin/**` does not change the
> running copy: you need `claude plugin marketplace update clawd` plus a reinstall
> (bump the version in `plugin.json` for a fresh cache). Setting the IP via the
> `settings.local.json` env avoids that step, which is why it is preferred.
