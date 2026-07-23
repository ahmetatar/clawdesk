#!/usr/bin/env bash
# clawd statusLine WRAPPER — Claude Code's statusLine.command is redirected to this
# (done by clawd-statusline-setup.sh). It does two jobs, feeding the stdin JSON to both:
#   1) mirror to the device: clawd-statusline-post.sh (in the background, NEVER blocks)
#   2) run the USER'S ORIGINAL statusLine command verbatim -> CLI output DOES NOT CHANGE
#
# The original ("inner") command is written by setup to $HOME/.claude/.clawd-statusline-inner
# (a file instead of an arg to avoid quoting/escape trouble). If empty (the user had no
# prior statusLine) only the device is mirrored, and the CLI shows an empty line.
set -u

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INNERFILE="$HOME/.claude/.clawd-statusline-inner"
INPUT="$(cat)"

# 1) mirror to the device (side-effect; does not block, errors swallowed)
printf '%s' "$INPUT" | bash "$DIR/clawd-statusline-post.sh" >/dev/null 2>&1 || true

# 2) produce the original statusLine output (stdin is fed again)
if [ -f "$INNERFILE" ]; then
  inner="$(cat "$INNERFILE" 2>/dev/null || true)"
  [ -n "$inner" ] && printf '%s' "$INPUT" | eval "$inner"
fi
