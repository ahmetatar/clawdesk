# clawd — Claude Code plugin'i

Claude Code hook olaylarını **clawd cihazına** (CYD/ESP32) `POST /e` ile yansıtır.
Maskot; sen kod yazarken düşünür, araç çalışırken "hack"ler, commit'te sevinir,
hata olduğunda üzülür, iş bitince dinlenir.

Firmware tarafı (`src/main.cpp` → `mapEvent`) bu olayları zaten karşılıyor; bu plugin
PC tarafındaki eksik köprüdür. Protokol: repo kökündeki `clawd-device-protocol.md`.

---

## Gereksinimler

1. **Çalışan bir clawd cihazı** — CYD'ye firmware flash'lanmış ve **kendi WiFi'na**
   bağlanmış olmalı. (Kurulum: repo kökü `README.md` + `include/secrets.h`'a kendi
   SSID/parolan; `pio run -t upload`.) PC ile cihaz **aynı ağda** olmalı.
2. **`jq` ve `curl`** — hook script'i bunları kullanır. macOS'te curl hazır; jq:
   `brew install jq`. (Kontrol: `jq --version`, `curl --version`.)
3. **Claude Code** — hook'lar buradan tetiklenir.

Yeni birine cihazı verdiğinde tek gerçek uğraş: (1) firmware'i kendi WiFi'yla bir
kez flash'lamak, (2) plugin'i kurmak, (3) mDNS ağında çalışmıyorsa cihazın IP'sini
bir yere yazmak (aşağıda). Kod tarafında elle düzenleme YOK.

---

## Kurulum (aktif projeye)

Plugin, repo içinde bir **yerel marketplace** olarak durur. Yolu **kendi klonuna
göre** ver (aşağısı örnek — `git clone` yaptığın dizini kullan):

```
# 1) marketplace'i tanıt (bir kez; <REPO> = klonladığın yol)
/plugin marketplace add <REPO>/tools/clawd-plugin

# 2) aktif projede etkinleştir (kişisel, gitignore'lu kapsam)
/plugin install clawd@clawd --scope local
```

Bu, projenin `.claude/settings.local.json`'ına `enabledPlugins` girdisi yazar.
Hook'lar **yeni** bir Claude Code oturumunda (ya da `/reload-plugins` sonrası)
devreye girer.

## Kaldırma

```
claude plugin disable clawd@clawd     # geçici kapat
claude plugin uninstall clawd@clawd   # tamamen kaldır
```

---

## Cihaz adresi — IP'ni nereye gireceksin

**Varsayılan hedef `clawd.local`** (mDNS). Ağında mDNS sağlıklıysa **hiçbir şey
girmene gerek yok**, kutudan çalışır.

Ağında mDNS yavaş/çalışmıyorsa (bazı router'larda olur — olaylar geç gelir/düşer),
cihazın **doğrudan IP'sini** ver. İki adım:

**1) Cihazın IP'sini öğren** — şu yollardan biri:
- Seri monitör: cihaz açılışta `[clawd] WiFi OK. IP: 192.168.x.y` yazar.
- Router'ın DHCP istemci listesi (host adı: `clawd`).
- mDNS bir kez çözülürse: `curl http://clawd.local/health` → cevap veren IP.

**2) IP'yi projenin `.claude/settings.local.json`'una yaz** (kişisel, gitignore'lu):

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

Claude Code bu `env`'i hook'a enjekte eder → isim çözme adımı tamamen atlanır,
sıfır DNS gecikmesi. **Kod dosyasına dokunmazsın**; her makine kendi IP'sini burada
tutar (bu dosya paylaşılmaz).

> **En sağlamı:** router'da cihaza **DHCP rezervasyonu** tanımla — IP kalıcı olarak
> sabit kalır, bir daha uğraşmazsın.

`CLAWD_TIMEOUT` (sn, varsayılan 2) ile curl üst sınırını da ayarlayabilirsin. curl
**arka planda** çalışır ve çıktısı `/dev/null`'a gider; bu yüzden timeout ne olursa
olsun Claude Code hiç beklemez (fire-and-forget, protokol §6).

---

## Olay eşlemesi

| Claude Code hook | gönderilen (`POST /e`) | cihaz animasyonu |
|---|---|---|
| `UserPromptSubmit` | `{"k":"prompt.submit",...}` | **think** |
| `PreToolUse` (tüm araçlar) | `{"k":"tool.pre",...}` | **hacking** (tekrar = no-op) |
| `PostToolUse` (Bash, git commit/push) | `{"k":"git",...}` | **happy** |
| `PostToolUseFailure` (tüm araçlar) | `{"k":"tool.post","d":{"ok":false}}` | **oops** |
| `PreCompact` | `{"k":"compact"}` | **think** |
| `SessionStart` | `{"k":"session.start"}` | **happy** |
| `Stop` | `{"k":"session.stop"}` | **idle** |

**Neden "event hell" yok:** firmware `if (id != curAnim) setAnim(...)` ile aynı
animasyona giden olayı yutar. Bir araç patlaması boyunca clawd tek bir `hacking`
state'inde kalır; sıradan araç *başarısı* hiç paket üretmez (yalnız git commit/push
kutlanır). Bir turun akışı: **think → hacking → (git'te happy) → oops(hata) → idle.**

---

## Test (cihaz olmadan / cihazla)

Script'i doğrudan besleyip davranışı görebilirsin:

```
# cihaz gerekmez — anında döner (erişilemez host)
echo '{"hook_event_name":"PostToolUseFailure","tool_name":"Bash"}' \
  | CLAWD_HOST=127.0.0.1:9 scripts/clawd-hook.sh

# gerçek cihaza idle yollar (IP'ni yaz)
echo '{"hook_event_name":"Stop"}' \
  | CLAWD_HOST=192.168.1.200 scripts/clawd-hook.sh
```

Cihaz canlılığı: `curl http://<IP>/health` →
`{"fw":"1.0.0","caps":["anim","led","touch","power","hud","status"]}`.

---

## statusLine → cihaz HUD üst bandı

Cihazın üst bandı, Claude Code'un **statusLine** özetini gösterir:
`Model  ctx:%  5h:%  wk:%` (yüzdeler kullanıma göre yeşil/sarı/kırmızı — statusLine
ile birebir). Veri `POST /status` ile gelir ve **güç yönetimini etkilemez** (cihazı
uyandırmaz/uyanık tutmaz).

**Neden ayrı kurulum?** Claude Code plugin'lere ana `statusLine`'ı manifest'ten
verdirmez (sadece `agent`/`subagentStatusLine`). Ayrıca `context_window`/`rate_limits`
verisine YALNIZ statusLine komutu erişir (hook'lar erişemez). Tek desteklenen yol:
`settings.json`'daki `statusLine.command`'i plugin'in **wrapper**'ına yönlendirmek.

**Kurulum (bir kez):**
```
/clawd:clawd-statusline          # slash komut, kurulumu çalıştırır
# ya da elle:
bash "<plugin>/scripts/clawd-statusline-setup.sh"
```
Kurulum senin **mevcut statusLine'ını AYNEN korur** (wrapper onu içinden çalıştırır,
CLI görünümü değişmez) ve yanında cihaza POST ekler. Geri almak: aynı script `--uninstall`.
Cihaz `clawd.local`'de değilse `CLAWD_HOST`'u `settings.local.json` env'inden ver.

---

## Spinner kelimeleri → cihaz (Claude Code'un GERÇEK listesi)

Cihaz HUD üst satırındaki "düşünme/çalışma" metinleri (spinner) artık firmware'e
gömülü **gerçek Claude Code gerund havuzundan** gelir (`src/spinner_words.h`, ~178
kelime — "Cogitating…", "Herding…", "Combobulating…"). CC bu kelimeyi hiçbir hook/
statusLine arayüzüne sızdırmadığı için ekrandaki **birebir o an**ki kelime alınamaz;
bunun yerine cihaz **aynı kelime havuzundan** kendisi seçer (WORK+THINK tek havuz;
HAPPY/OOPS cihaza özel kalır — CC başarıda/hatada spinner kelimesi göstermez).

Listeyi ileride özelleştirir/güncellersen cihaza yansıtmak için:

```
/clawd:sync-spinner-words        # slash komut: havuzu cihaza POST /words ile gönder
# önce önizleme:
bash "<plugin>/scripts/clawd-spinner-sync.sh" --preview
```

- **Kaynak:** `~/.claude/clawd-spinner-words.txt` varsa (her satır bir kelime; `#`
  yorum) o dosya kaynak alınır; yoksa kurulu Claude Code ikilisinden **canlı** çıkarılır
  (sürümden bağımsız — nadir bir çapa kelimeyle küme bulunur).
- **Kalıcılık:** liste cihazın **RAM**'inde tutulur; reboot'ta `spinner_words.h`
  varsayılanına döner. Özel listen varsa reboot sonrası komutu tekrar çalıştır.
- **Compile-time varsayılanı tazele** (yeni CC sürümü):
  `bash "<plugin>/scripts/clawd-spinner-extract.sh" --header > src/spinner_words.h`
  → firmware'i yeniden flash'la.

Cihaz adresi yine `CLAWD_HOST` (varsayılan `clawd.local`) ile verilir.

---

## Dosyalar

```
tools/clawd-plugin/
  .claude-plugin/marketplace.json     # yerel marketplace (name: clawd)
  plugins/clawd/
    .claude-plugin/plugin.json        # plugin manifesti
    hooks/hooks.json                  # 8 hook olayı -> clawd-hook.sh
    commands/clawd-statusline.md       # /clawd:clawd-statusline (statusLine kurulumu)
    commands/sync-spinner-words.md     # /clawd:sync-spinner-words (spinner havuzu -> cihaz)
    scripts/clawd-hook.sh             # olay köprüsü (jq + curl -> POST /e)
    scripts/clawd-statusline-post.sh   # statusLine JSON -> POST /status (throttle'lı)
    scripts/clawd-statusline.sh        # statusLine wrapper (orijinali sarar + cihaz)
    scripts/clawd-statusline-setup.sh  # settings.json statusLine'ı wrapper'a bağlar
    scripts/clawd-spinner-extract.sh   # CC spinner kelimelerini çıkar (--header ile spinner_words.h)
    scripts/clawd-spinner-sync.sh      # spinner havuzunu cihaza gönder (POST /words)
    README.md
```

> **Geliştirici notu:** local-dizin kurulumu plugin'i
> `~/.claude/plugins/cache/`'e **kopyalar**. `tools/clawd-plugin/**` altını
> düzenlersen çalışan kopya değişmez: `claude plugin marketplace update clawd` +
> reinstall gerekir (yeni cache için `plugin.json` sürümünü artır). IP'yi
> `settings.local.json` env'inden vermek bu adımı gerektirmez — tercih sebebi.
