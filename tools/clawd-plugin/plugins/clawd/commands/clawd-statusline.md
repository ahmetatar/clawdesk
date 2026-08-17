---
description: Connect the clawd device to this machine's Claude Code statusLine (device HUD top band = model + context% + quota%)
---

Set up the clawd device statusLine on this machine.

Run this command (it wraps the existing status line verbatim and adds `POST /status` to the device; `--uninstall` reverts it):

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-statusline-setup.sh"`

If `${CLAUDE_PLUGIN_ROOT}` does not resolve, locate the clawd plugin's `scripts/clawd-statusline-setup.sh` and run that.

Then tell the user:
- The existing status line is preserved (the CLI looks unchanged); it runs inside the wrapper.
- The device's top band now shows model + context% + 5h% + wk% (percentages colored green/yellow/red by usage).
- If the device is not at `clawd.local`, set `CLAWD_HOST` in the project's `.claude/settings.local.json` env block (e.g. `"CLAWD_HOST": "192.168.1.200"`).
- To revert: run the same script with `--uninstall`.
