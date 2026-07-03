---
description: clawd cihazinin spinner kelime havuzunu Claude Code'un GERCEK listesiyle esitle (POST /words)
---

clawd cihazinin spinner (dusunme/calisma) kelime havuzunu bu makinedeki Claude Code'un
gercek listesiyle esitle.

Once (istersen) onizle, sonra gonder:

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-sync.sh" --preview`

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-sync.sh"`

Eger `${CLAUDE_PLUGIN_ROOT}` cozulmezse, clawd plugin'inin `scripts/clawd-spinner-sync.sh`
dosyasini bul ve calistir.

Sonra kullaniciya sunlari soyle:
- Cihazin spinner kelimeleri artik Claude Code'un GERCEK gerund havuzundan gelir
  (or. "Cogitating...", "Herding...", "Combobulating..."). HUD ust satirinda gorunur.
- **Kalicilik:** liste cihazin RAM'inde tutulur; **reboot'ta derleme-ici varsayilana
  (spinner_words.h) doner**. Ozel listen varsa reboot sonrasi bu komutu tekrar calistir.
- **Ozellestirme:** `~/.claude/clawd-spinner-words.txt` olusturursan (her satir bir
  kelime; `#` yorum, bos satir atlanir) sync o dosyayi KAYNAK alir. Dosya yoksa kurulu
  Claude Code ikilisinden canli cikarilir (surumden bagimsiz).
- Cihaz `clawd.local`'de degilse `CLAWD_HOST`'u proje `.claude/settings.local.json` env
  blogundan ver (or. `"CLAWD_HOST": "192.168.1.201"`).
- Compile-time varsayilani tazelemek (yeni CC surumu) icin:
  `bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-spinner-extract.sh" --header > src/spinner_words.h`
  ve firmware'i yeniden flash'la.
