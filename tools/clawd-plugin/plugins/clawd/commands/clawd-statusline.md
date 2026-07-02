---
description: clawd cihazini bu makinenin Claude Code statusLine'ina bagla (device HUD ust bandi = model + context% + kota%)
---

Bu makinede clawd device statusLine kurulumunu yap.

Su komutu calistir (mevcut status line'i AYNEN sarar + cihaza `POST /status` ekler; geri almak icin `--uninstall`):

`bash "${CLAUDE_PLUGIN_ROOT}/scripts/clawd-statusline-setup.sh"`

Eger `${CLAUDE_PLUGIN_ROOT}` cozulmezse, clawd plugin'inin `scripts/clawd-statusline-setup.sh` dosyasini bul ve calistir.

Sonra kullaniciya sunlari soyle:
- Mevcut status line'i KORUNDU (CLI gorunumu degismez), wrapper icinden calisir.
- Cihaz ust bandi artik model + context% + 5h% + wk% gosterir (yuzdeler kullanima gore yesil/sari/kirmizi).
- Cihaz `clawd.local`'de degilse, `CLAWD_HOST`'u proje `.claude/settings.local.json` env blogundan ver (or. `"CLAWD_HOST": "192.168.1.200"`).
- Geri almak: ayni script'i `--uninstall` ile calistir.
