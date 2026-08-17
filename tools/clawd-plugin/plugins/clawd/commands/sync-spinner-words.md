---
description: Sync the clawd device's spinner word pool with Claude Code's real list (POST /words)
---

Sync the clawd device's spinner (thinking/working) word pool with the real list from
the Claude Code installed on this machine.

Preview first if you like, then send:

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-sync.sh" --preview`

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-sync.sh"`

If `${CLAUDE_PLUGIN_ROOT}` does not resolve, locate the clawd plugin's
`scripts/clawd-spinner-sync.sh` and run that.

Then tell the user:
- The device's spinner words now come from Claude Code's real gerund pool
  (e.g. "Cogitating...", "Herding...", "Combobulating..."), shown on the HUD's top line.
- **Persistence:** the list lives in the device's RAM and **reverts to the compiled-in
  default (spinner_words.h) on reboot**. Re-run this command after a reboot to restore
  a custom list.
- **Customisation:** if you create `~/.claude/clawd-spinner-words.txt` (one word per
  line; `#` comments and blank lines are skipped), the sync uses that file as its
  source. Without it, the words are extracted live from the installed Claude Code
  binary, independent of version.
- If the device is not at `clawd.local`, set `CLAWD_HOST` in the project's
  `.claude/settings.local.json` env block (e.g. `"CLAWD_HOST": "192.168.1.201"`).
- To refresh the compile-time default (new CC release):
  `bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-extract.sh" --header > src/spinner_words.h`
  then reflash the firmware.
