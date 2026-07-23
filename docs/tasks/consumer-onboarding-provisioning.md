# TASK: Tüketici onboarding — kutudan flashlı cihaz + runtime WiFi provisioning

**Durum:** BEKLEMEDE (sonra bakılacak)
**Oluşturma:** 2026-07-19
**Amaç:** Satılan cihaz için "satın al → plugin indir → tek komut çalıştır → clawd hazır" akışını gerçekten mümkün kılmak.

## Hedef akış (müşteri)

1. Cihazı kutudan çıkarır (firmware **önceden flashlı**, kaynak/toolchain gerekmez).
2. Telefonuyla cihazın SoftAP'ine bağlanır, kendi WiFi'sini seçip şifre girer.
3. PC'de **tek komut** çalıştırır (plugin global install + statusLine + `CLAWD_HOST`).
4. clawd hazır.

## Bugün neden sağlanmıyor (2026-07-19 analizi)

Mevcut akış "kaynaktan derle + USB flashla" = geliştirici akışı. Blocker'lar:

1. **WiFi derleme-anında gömülü (asıl blocker).** `include/secrets.h` → `#define WIFI_SSID/WIFI_PASS`; `connectWiFi()` bu makroları kullanıyor. WiFi değiştirmek = yeniden derle + flashla. Runtime provisioning yok.
2. **Müşteri toolchain + kaynak koda muhtaç.** `install.sh` firmware'i kaynaktan derliyor (`pio run -t upload`). Ön koşul: platformio, jq, curl, claude-code + repo.
3. **Statik IP de derleme-anında** (`include/config.h` satır 95-99) ve belirli bir ağa çakılı. Gönderilen default alıcının ağı için yanlış.
4. **PC'nin cihaz IP'sini bulması flash'a bağlı.** `install.sh` IP'yi flash sırasında seri porttan yakalıyor. mDNS (`clawd.local`) extender arkasında güvenilmez.

## Bugün zaten çalışan

PC tarafı "tek komuta" yakın ve sağlam: plugin global install, statusLine köprüsü, spinner sync, `CLAWD_HOST` env. Eksik olan **firmware yarısı** (provisioning).

## Yapılacaklar

### A) Firmware — kendini provision eden (linchpin)
- [ ] İlk açılışta NVS'de (`Preferences`) kayıtlı WiFi yoksa **SoftAP + captive portal** başlat (`clawd-setup` ağı).
- [ ] Portal: müşteri WiFi seçip şifre girer → kimlik NVS'ye yazılır → reboot → bağlanır.
- [ ] `connectWiFi()`'yi "NVS'den oku, yoksa portal aç" olacak şekilde değiştir. `include/secrets.h` bağımlılığını kaldır.
- [ ] Default **DHCP** (derleme-anı statik IP'yi bırak). Statik gerekiyorsa çalışma-anında ilk bağlanışta türet.
- [ ] Bağlandıktan sonra cihazın IP'sini **ekranda** (ve portalda) göster → müşteri PC komutuna girer. mDNS güvenilmezliğini bu şekilde aş.
- [ ] "WiFi'yi sıfırla" yolu (uzun dokunuş / özel jest) → NVS temizle, tekrar portal.

### B) PC — tek komut
- [ ] `install.sh`'i ikiye ayır: firmware derleme/flash **müşteride yok**. Kalan komut sadece plugin global install + statusLine wire + `CLAWD_HOST` set (mevcut parçalar).
- [ ] `CLAWD_HOST` girişi: müşteri portalın/ekranın gösterdiği IP'yi verir (veya mDNS dener).

### C) Üretim/dağıtım
- [ ] Fabrika flash prosedürü: tek `firmware.bin` (WiFi'siz, provisioning'li) tüm cihazlara.
- [ ] README/kutu içi: "telefonla WiFi ver + PC'de tek komut" kısa kılavuz.

## Notlar
- İlgili hafıza: `clawd-host-use-ip` (mDNS extender arkasında timeout), `clawd-install-uninstall-scripts`, `device-memory-budget` (huge_app partition — captive portal HTTP + DNS ekstra flash yer ister, kontrol et).
- SoftAP captive portal için ESP32'de `DNSServer` + `WebServer/AsyncWebServer` gerekli; mevcut `AsyncWebServer` zaten var.
