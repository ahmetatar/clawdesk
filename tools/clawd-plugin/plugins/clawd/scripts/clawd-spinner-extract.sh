#!/usr/bin/env bash
# clawd spinner kelime CIKARICI — Claude Code'un "gerund" spinner havuzunu uretir.
# STDOUT'a HER SATIRDA BIR KELIME yazar (dedup + alfabetik). Baska hicbir sey yazmaz;
# uyarilar stderr'e gider. Iki kaynak, su oncelikle:
#
#   1) KULLANICI OZELI: $HOME/.claude/clawd-spinner-words.txt varsa (bos degilse), o
#      dosya KAYNAK KABUL edilir (her satir bir kelime; bos satir ve '#' yorum atlanir).
#      Boylece listeyi Claude Code'da/elle ozellestirip cihaza aynen yansitilabilir.
#
#   2) VARSAYILAN: kurulu Claude Code ikilisinden (native binary ya da JS bundle) canli
#      cikarim. Spinner kelimeleri ikili icinde tek bir kume olusturur; nadir bir
#      "capa" kelimenin (or. Flibbertigibbeting) etrafindaki pencerede gecen tum
#      "[A-Z][a-z]+ing" gerund'lari toplanir -> surumden bagimsiz (ofset sabit degil,
#      capa aramayla bulunur), kod-string false-positive'leri (Cstring, Ceiling...)
#      pencere disinda kaldigi icin ELENIR.
#
# Kullanim:
#   clawd-spinner-extract.sh            # kelimeleri bas (sync/preview)
#   clawd-spinner-extract.sh --count    # yalniz adet
#   clawd-spinner-extract.sh --header   # src/spinner_words.h govdesini bas (C dizi)
set -u

CUSTOM="$HOME/.claude/clawd-spinner-words.txt"

emit_words() {
  if [ -s "$CUSTOM" ]; then
    # kullanici ozeli: yorum/bos ele, bosluk kirp, dedup, sirala
    grep -vE '^[[:space:]]*(#|$)' "$CUSTOM" \
      | tr -d '[:space:]' \
      | awk 'NF && !seen[$0]++' \
      | sort
    return
  fi

  bin="$(command -v claude 2>/dev/null || true)"
  if [ -z "$bin" ]; then
    echo "clawd-spinner-extract: 'claude' bulunamadi (PATH'te yok)." >&2
    return 1
  fi
  command -v python3 >/dev/null 2>&1 || {
    echo "clawd-spinner-extract: python3 gerekli (ikiliden cikarim icin)." >&2
    return 1
  }

  python3 - "$bin" <<'PY'
import os, re, sys
binpath = os.path.realpath(sys.argv[1])            # symlink zincirini coz -> gercek ikili/bundle
try:
    data = open(binpath, "rb").read().decode("latin-1")
except OSError as e:
    sys.stderr.write("clawd-spinner-extract: okunamadi: %s\n" % e); sys.exit(1)

# Nadir, YALNIZ spinner havuzunda gecen capa kelimeler -> kumeyi bul.
anchors = ["Flibbertigibbeting", "Combobulating", "Boondoggling",
           "Razzmatazzing", "Discombobulating", "Whatchamacalliting"]
off = -1
for a in anchors:
    i = data.find('"%s"' % a)
    if i >= 0:
        off = i; break
if off < 0:
    sys.stderr.write("clawd-spinner-extract: spinner capasi bulunamadi "
                     "(Claude Code surumu spinner listesini degistirmis olabilir).\n")
    sys.exit(2)

lo = max(0, off - 24000); hi = off + 24000        # capa etrafinda ~48KB pencere
win = data[lo:hi]
seen, words = set(), []
for m in re.finditer(r'"([A-Z][a-z]+ing)"', win):  # tirnakli tek-kelime gerund
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
'// GENERATED — DUZENLEME! `tools/clawd-plugin/plugins/clawd/scripts/clawd-spinner-extract.sh --header`' \
'// ile yeniden uretilir. Claude Code spinner ("gerund") kelime havuzu (derleme-ici' \
'// varsayilan). Calisma-zamaninda POST /words ile degistirilebilir (clawd:sync-spinner-words).'
    printf 'static const char *const SPINNER_WORDS_DEFAULT[] = {\n'
    printf '%s\n' "$words" | awk 'NF{ printf "  \"%s\",\n", $0 }'
    printf '};\n'
    printf 'static const int SPINNER_WORDS_DEFAULT_N = %s;\n' "$n" ;;
  ""|--list)
    emit_words ;;
  *)
    echo "kullanim: clawd-spinner-extract.sh [--list|--count|--header]" >&2; exit 2 ;;
esac
