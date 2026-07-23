#!/usr/bin/env bash
# clawd spinner word EXTRACTOR — produces Claude Code's "gerund" spinner pool.
# Writes ONE WORD PER LINE to STDOUT (deduped + alphabetical). Nothing else goes to
# stdout; warnings go to stderr. Two sources, in this priority:
#
#   1) USER-SPECIFIC: if $HOME/.claude/clawd-spinner-words.txt exists (and is non-empty),
#      that file is treated as the SOURCE (one word per line; blank lines and '#' comments
#      are skipped). This lets you customize the list in Claude Code / by hand and mirror
#      it to the device verbatim.
#
#   2) DEFAULT: a live extraction from the installed Claude Code binary (native binary or
#      JS bundle). The spinner words form a single cluster in the binary; all
#      "[A-Z][a-z]+ing" gerunds within a window around a rare "anchor" word
#      (e.g. Flibbertigibbeting) are collected -> version-independent (the offset isn't
#      fixed, it's found by anchor search), and code-string false positives (Cstring,
#      Ceiling...) are EXCLUDED because they fall outside the window.
#
# Usage:
#   clawd-spinner-extract.sh            # print the words (sync/preview)
#   clawd-spinner-extract.sh --count    # count only
#   clawd-spinner-extract.sh --header   # print the src/spinner_words.h body (C array)
set -u

CUSTOM="$HOME/.claude/clawd-spinner-words.txt"

emit_words() {
  if [ -s "$CUSTOM" ]; then
    # user-specific: drop comments/blanks, trim whitespace, dedup, sort
    grep -vE '^[[:space:]]*(#|$)' "$CUSTOM" \
      | tr -d '[:space:]' \
      | awk 'NF && !seen[$0]++' \
      | sort
    return
  fi

  bin="$(command -v claude 2>/dev/null || true)"
  if [ -z "$bin" ]; then
    echo "clawd-spinner-extract: 'claude' not found (not on PATH)." >&2
    return 1
  fi
  command -v python3 >/dev/null 2>&1 || {
    echo "clawd-spinner-extract: python3 required (for binary extraction)." >&2
    return 1
  }

  python3 - "$bin" <<'PY'
import os, re, sys
binpath = os.path.realpath(sys.argv[1])            # resolve the symlink chain -> real binary/bundle
try:
    data = open(binpath, "rb").read().decode("latin-1")
except OSError as e:
    sys.stderr.write("clawd-spinner-extract: could not read: %s\n" % e); sys.exit(1)

# Rare anchor words that appear ONLY in the spinner pool -> locate the cluster.
anchors = ["Flibbertigibbeting", "Combobulating", "Boondoggling",
           "Razzmatazzing", "Discombobulating", "Whatchamacalliting"]
off = -1
for a in anchors:
    i = data.find('"%s"' % a)
    if i >= 0:
        off = i; break
if off < 0:
    sys.stderr.write("clawd-spinner-extract: spinner anchor not found "
                     "(the Claude Code version may have changed the spinner list).\n")
    sys.exit(2)

lo = max(0, off - 24000); hi = off + 24000        # ~48KB window around the anchor
win = data[lo:hi]
seen, words = set(), []
for m in re.finditer(r'"([A-Z][a-z]+ing)"', win):  # quoted single-word gerund
    w = m.group(1)
    if w not in seen:
        seen.add(w); words.append(w)
for w in sorted(words):
    print(w)
PY
}

case "${1:-}" in
  --count)
    emit_words | grep -c . ;;
  --header)
    words="$(emit_words)" || exit 1
    n="$(printf '%s\n' "$words" | grep -c .)"
    printf '%s\n' \
'#pragma once' \
'// GENERATED — DO NOT EDIT! Regenerate with' \
'// `tools/clawd-plugin/plugins/clawd/scripts/clawd-spinner-extract.sh --header`.' \
'// Claude Code spinner ("gerund") word pool (compile-time default). Can be changed at' \
'// runtime via POST /words (clawd:sync-spinner-words).'
    printf 'static const char *const SPINNER_WORDS_DEFAULT[] = {\n'
    printf '%s\n' "$words" | awk 'NF{ printf "  \"%s\",\n", $0 }'
    printf '};\n'
    printf 'static const int SPINNER_WORDS_DEFAULT_N = %s;\n' "$n" ;;
  ""|--list)
    emit_words ;;
  *)
    echo "usage: clawd-spinner-extract.sh [--list|--count|--header]" >&2; exit 2 ;;
esac
