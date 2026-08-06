# clawd — ana uygulama

Fiziksel Claude Code maskotu. **CYD** (ESP32-2432S028R "Cheap Yellow Display")
üzerinde çalışır, WiFi'dan olay alır ve 5 ifade animasyonuyla tepki verir.

> Cihazın yeteneklerini keşfeden örnekler `examples/` altında; bu kök proje
> artık **gerçek uygulamadır**. Animasyon üretim hattı `tools/pixellab/`.

## Özellikler

- **14 ifade animasyonu** + gezinen mini maskot, 64×64 RGB565, landscape ekrana
  3× ölçekli, her biri kendi fps'inde. (`src/anims/`) Hepsini oynar halde görmek
  için **[maskot vitrini](docs/showcase.html)** — tarayıcıda aç; `python3
  tools/build_showcase.py` ile `src/anims/*.h`'den yeniden üretilir.
- **Olay protokolü** — `POST /e` (fire-and-forget, 204). Olay → animasyon eşlemesi
  (`tool.pre`→hacking, `git`→happy, `think`→think, `tool.post ok=false`→oops …).
  `GET /health` canlılık. mDNS `clawd.local` cihazda çalışır ama yalnız keşif/yedek
  içindir — PC entegrasyonu (`install.sh`) her zaman **sabit IP** kullanır, çünkü
  mDNS bazı ağlarda event iletimini geciktirir/koparır.
- **Güç yönetimi** (ilk özellik — `src/power.h`):

  | Durum  | Tetik              | Davranış |
  |--------|--------------------|----------|
  | ACTIVE | olay/dokunma       | Tam parlaklık, animasyon oynar |
  | DIM    | 20 sn olaysız      | Arka ışık ~%11 (yumuşak fade), animasyon oynamaya devam |
  | SLEEP  | 90 sn olaysız      | Ekran söner, animasyon durur, CPU 80 MHz, WiFi modem-sleep |

  **Uyanma:** gelen `POST /e` paketi (WiFi association korunduğu için CPU'yu
  uyandırır) veya dokunmatik → anında tam parlaklığa fade + 240 MHz + animasyon.
  Eşikler/parlaklık `include/config.h` içinde.

## Kurulum (sıfırdan, tek komut)

Cihazı eline alan biri tek script çalıştırır, gerisini script sorar/yapar:

```sh
./install.sh
```

Sırasıyla: WiFi bilgilerini `include/secrets.h`'a yazar → **bağlanılacak ağın
gateway/subnet'ini otomatik tespit edip cihaza ZORUNLU bir sabit IP atar**
(event iletiminde mDNS/DHCP gecikmesi veya IP değişimi olmasın diye — soru
sorar ama bu adım atlanamaz, sadece önerilen değerler onaylanır/değiştirilir)
→ cihazı USB'den flaslar → clawd plugin'ini bu makinede **global** kurar
(`claude plugin install clawd@clawd --scope user` — her projede aktif olur, tek bu
repoda değil) → statusLine HUD köprüsünü bağlar (`/clawd:clawd-statusline`'ın
yaptığının aynısı) → spinner kelime havuzunu cihaza eşitler. Tekrar çalıştırmak
zararsız (idempotent); flash/plugin/statusLine/spinner adımlarını atlayabilirsin,
sabit IP adımı hariç.

Ağ değiştirdiğinde (ör. eve dönünce ya da hotspot'a geçince) `./install.sh`'i
tekrar çalıştırman yeterli — yeni ağın gateway/subnet'ini otomatik tespit edip
sabit IP'yi ona göre yeniden yazar.

**Geri almak / cihazı başka birine devretmek:**

```sh
./uninstall.sh
```

Global plugin'i, statusLine köprüsünü ve `CLAWD_HOST` ayarını bu makineden
tamamen kaldırır; sabit IP'yi **eski bir değere döndürmez, doğrudan kapatır**
(`CLAWD_STATIC_IP` → 0, cihaz DHCP'ye döner) ve `include/secrets.h`'ı (sorarak)
siler. Cihazın firmware'ine dokunmaz — fişten çekmek yeterli.

## Manuel / gelişmiş kurulum

`install.sh`'in tek tek yaptığı adımlar, elle çalıştırmak istersen:

```
platformio.ini        huge_app partition (5 anim + ağ yığını 1.25MB'a sığmaz)
include/config.h      tüm ayarlanabilir sabitler (pin, süre, parlaklık, sabit IP)
include/secrets.h      WiFi bilgileri (.gitignore'da)
src/main.cpp          setup/loop: WiFi + server + animasyon + güç
src/power.h           PowerManager: iki kademeli idle + arka ışık fade
src/anims.h           animasyon kaydı (fps, geçici mi, LED rengi)
src/anims/clawd_*.h   PixelLab ile üretilmiş kare verileri
tools/clawd-plugin/   Claude Code plugin'i (hook → POST /e, statusLine, spinner)
```

WiFi bilgilerini `include/secrets.h`'a yaz (`WIFI_SSID`/`WIFI_PASS`). Sabit IP
istiyorsan (önerilir — bkz. yukarısı) `include/config.h`'da `CLAWD_STATIC_IP` ve
`IP_LOCAL`/`IP_GATEWAY`/`IP_SUBNET`/`IP_DNS`'i ağına göre elle ayarla. Sonra derle & yükle:

```sh
SSL_CERT_FILE=~/.platformio/system-ca-bundle.pem REQUESTS_CA_BUNDLE=~/.platformio/system-ca-bundle.pem \
  ~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbserial-XXXX
```

Claude Code entegrasyonu için ayrıntı: `tools/clawd-plugin/plugins/clawd/README.md`.
`CLAWD_HOST`'u global `~/.claude/settings.json`'a elle yazman gerekir (`install.sh`
bunu otomatik yapar).

Test (`<cihaz-ip>` = cihazın gerçek IP'si; `jq '.env.CLAWD_HOST' ~/.claude/settings.json` ile bulabilirsin):
```sh
curl http://<cihaz-ip>/health
curl -X POST http://<cihaz-ip>/e -H 'Content-Type: application/json' -d '{"k":"git","d":{"s":"commit"}}'
```
