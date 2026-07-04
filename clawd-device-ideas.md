# clawd Masaüstü Maskotu — Fikir Dökümanı

> **Konsept:** Claude Code maskotu **clawd**'ı, ESP32 tabanlı bir TFT ekran cihazında (CYD — "Cheap Yellow Display" / ESP32-2432S028R) fiziksel olarak yaşatmak. Ben (Claude Code) çalışırken cihaz gerçek zamanlı, sevimli ve komik tepkiler verir.

---

## Donanım: Elimizdeki kart (CYD / ESP32-2432S028R)

Robotistan'ın listesindeki temel speclere ek olarak bu kartta tipik olarak şunlar var:

- **2.8" renkli TFT ekran (ILI9341, 240x320)** → clawd'ın yüzü / animasyon tuvali
- **Dirençli dokunmatik (XPT2046)** → kafasına dokun, sev, izin ver/iptal et
- **RGB LED** → duygu durumu / "düşünüyorum" nefes ışığı
- **LDR ışık sensörü** → oda karanlıksa "uyku" modu
- **Hoparlör çıkışı** → Tamagotchi tarzı ufak sesler
- **SD kart yuvası** → animasyon kareleri, sesler, clawd skinleri
- **WiFi + BLE** → PC ile kablosuz haberleşme
- **USB-C** → güç + programlama + seri haberleşme

---

## Beyin: Claude Code hook event'leri

Tüm fikirlerin temeli bu sinyaller. Claude Code hook'ları her olayda stdin'e JSON basar; bunu cihaza akıtınca clawd canlanır.

| Hook olayı | Ne zaman tetiklenir | Elimizdeki veri |
|---|---|---|
| `SessionStart` | Oturum başlar/resume | cwd, model |
| `UserPromptSubmit` | Sen prompt gönderince | promptun metni |
| `PreToolUse` | Bir araç çalışmadan **önce** | araç adı + inputu (üstelik **bloklayabilir**) |
| `PostToolUse` | Araç bitince | sonuç / hata |
| `Notification` | İzin gerekince / boşta kalınca | mesaj |
| `Stop` | Cevabımı bitirince | — |
| `SubagentStop` | Bir alt-ajan bitince | — |
| `PreCompact` | Context dolup sıkıştırmadan önce | — |
| `SessionEnd` | Oturum biter | — |

Ek kanal: **statusline** script'i her render'da model + context %'si + maliyet bilgisini verir → "canlı sağlık göstergesi" için.

---

## A) Canlı durum yansıtma — clawd ne yaptığımı "yaşar"

- **Araca göre kostüm/animasyon.** `PreToolUse`'taki `tool_name`'e göre clawd kılık değiştirir:
  - `Bash` → hacker modu, terminal tutar, gözlükler iner 🕶️
  - `Edit`/`Write` → eline kalem alır, yazıyor
  - `Read`/`Grep`/`Glob` → büyüteçle bir şey arıyor 🔍
  - `WebFetch`/`WebSearch` → dürbünle ufka bakar / internette sörf yapar 🏄
  - `Task`/subagent → clawd **kendini klonlar**, mini-clawd'lar koşuşturur (fan-out görselleştirmesi!)
- **"Düşünüyorum" nefes ışığı.** Ben düşünürken RGB LED, Claude Code'un kendi spinner'ı gibi yavaşça nefes alır. Cevap bitince (`Stop`) söner.
- **Context/enerji barı.** Statusline'dan context %'sini al, ekranda clawd'ın "kahve/enerji" çubuğu olarak göster. Doldukça clawd yorgunlaşır.

## B) Komik olay tepkileri — projenin ruhu

- **Hata = komedi.** `PostToolUse`'ta sonuç hatalıysa clawd facepalm / "ıngh" sesi. Test geçti → konfeti + yeşil LED + "ta-da!". Test patladı → ufak panik + sad trombone 🎺.
- **`git commit` kutlaması.** Bash inputunda `git commit` görürse clawd bayrak diker; `git push` → roket fırlatır 🚀.
- **`PreCompact` = "beyin defragı".** Context sıkışınca clawd kafasını tutup "uff hafızam doldu" animasyonu yapar, sonra silkinip devam eder.
- **Prompt'una tepki.** `UserPromptSubmit`'te metne bakıp: "fix the bug" → parmaklarını çıtlatır; çok uzun prompt → gözleri büyür "vay be"; küfür/sinir → anlayışlı bakar 😅. (Basit keyword/sentiment yeter.)
- **Uzun komut bekleyişi.** Bash uzun sürerse (build vb.) clawd ıslık çalıp ayak sallar, sıkılır.

## C) Fiziksel etkileşim — cihazdan **Claude Code'a** (killer feature)

`PreToolUse` hook'u **senkron çalışır ve aracı bloklayabilir.** Bu yüzden:

- **Dokunarak izin ver/reddet.** İzin gerektiren bir araçta clawd ekrana vurur, LED atar, "hey!" çalar. **Kafasına dokun → "evet çalıştır"**, yana kaydır → "iptal". Hook script'i cevabını dokunmatikten bekleyip allow/deny döner. Monitöre bakmadan fiziksel izin yönetimi.
- **"Bekliyorum" hatırlatıcısı.** `Stop`/`Notification` sonrası clawd sana bakıp ayak tıklatır; uzun süre dönmezsen (LDR ışık da kısıksa) uyuyakalır 😴.
- **Pet et.** Boşta clawd'a dokununca mırlar / kalp çıkarır.

## D) Donanımı sömüren dokunuşlar

- **LDR**: oda kararınca "geç oldu" modu, clawd esner.
- **Hoparlör**: her olaya Tamagotchi tarzı ufak çıtırtılar.
- **SD kart**: animasyon kareleri + ses + farklı clawd skinleri.

## E) İleri seviye

- **Sesli "clawd, ne yapıyorsun?"** → I2S mikrofon eklenir, ham ses PC'ye gider, Claude özet verir, clawd dudak oynatır.
- **Multi-agent panosu.** Workflow/Task ile birçok ajan koşarken her birini ekranda mini clawd olarak gör.
- **Günün özeti** (`SessionEnd`): kaç araç, kaç dosya, kaç commit — clawd küçük rapor sunar.

## F) v2 wow fikirleri (beyin fırtınası)

Aşağıdakiler mevcut protokole (`clawd-device-protocol.md`) dayanır; çoğu **yeni protokol olayı gerektirmez**, cihaz zaten aldığı olaylardan türetir.

### F1) Ters kanal — cihazdan Claude Code'a (izin'in ötesi)
Şu an sadece `/perm` cihaz→PC. Onu genişletmek en büyük etki:
- **Dokun → "devam et" enjekte et.** `Stop`'tan sonra clawd sana bakarken kafasına dokununca cihaz bir "kuyruğa alınmış prompt" tutar (`continue` / son promptu tekrarla). Bir sonraki hook onu çeker → masaüstü "ileri" tuşu.
- **Basılı tut → interrupt.** Uzun/yanlış giden işte basılı tutmak bir "iptal bayrağı" set eder; koşan `PreToolUse`/poll bunu görüp `deny`'a düşer. Fiziksel ESC.
- **Swipe → hızlı yanıt.** İzin dışı "evet/hayır/1/2" tarzı Notification'larda yön kaydırması = seçim.

### F2) Gerçek veriyle drama — HUD `wk` (kota) ve `ctx`'i sömür
- **Kota tükeniyor paniği.** Haftalık kota %90'ı geçince clawd terler, ışıkları kısar, "patron token bitiyor" modu; %100'de mola/uyku. Veri zaten `/status`'ta.
- **Context = kahve/enerji** (A'daki barın karakter hâli): `ctx` doldukça clawd yorulur, `compact`'ta defrag → sonra taze döner.
- **Commit ağırlığı.** `git commit`'te diff büyüklüğüne göre clawd ya hafifçe bayrak diker ya da devasa commit'i zor kaldırır 🏋️ (hook diff satır sayısını `d`'ye koyar).

### F3) Kalıcı Tamagotchi — NVS'te durum
- **Kaç gün üst üste kodladın (streak).** Karar verilen tasarım (v1 için ertelendi):
  - **Gün ölçütü:** o gün en az bir `edit` (Edit/Write/MultiEdit) **veya** `git commit/push`; sadece açıp bakmak saymaz. Cihaz zaten `tool.pre g=edit` / `git` alıyor → protokol değişmez.
  - **State NVS'te:** `streak`, `best`, `lastDay` — `lastDay` **epoch-gün** (NTP epoch/86400 + tz) olarak, `+1` aritmetiği temiz.
  - **Gösterim (çapa):** günün ilk `edit`/`commit`'inde (streak'in gerçekten arttığı an) ~2 sn "🔥 DAY N" kutlaması, sonra normal HUD. Aynı gün 2. oturumda tekrar patlamaz.
  - **Kırılma:** aradan sonra streak 1'e döner; kısa "seri kırıldı, en iyin N'di" hüzün anı.
  - **Notlar:** NTP senkron olmadan sayaca dokunma; bu "clawd'ın tanık olduğu" günler (cihaz kapalıyken görmez). Milestone (7/30/100) özel kutlaması sonraki katman.
- **Günün karnesi tarihçesi.** `session.end` özetini sakla; dokunmatikle **günü geri sar** — o günün araç zaman çizelgesini hızlandırılmış animasyon oynat (touch = scrubber).

### F4) Masadan uzakken "gel buraya" — kademeli çağrı
`Notification kind=idle` (Claude senin girdini bekliyor) gelince clawd önce el sallar, cevapsız kalırsan çırpınır, en sonda belirgin ses/animasyon. İzin ekranını kaçırmama fiziksel dürtü — cihazı "gerekli" yapan tek özellik bu olabilir.

### F5) Sonifikasyon — "kulakla kod takibi"
Her tool grubuna (`exec/edit/read/web/agent`) kısa, ayırt edilebilir ton. Oturum bir ritme dönüşür; ekrana bakmadan "şimdi build / şimdi düzenliyor" duyarsın. Başarı akışı da müzikleşir (hata=sad trombone zaten B'de).

### F6) Ambient ruh hali — oturum termometresi
`tool.post ok` başarı/hata oranına göre **ekran arka plan** renk sıcaklığı yavaşça kayar → göz ucuyla "oturum iyi mi kötü mü" hissi.
> ⚠️ Bu kartta saf kırmızı LED (GPIO4) ve LDR ölü — bunu RGB üzerinden değil **ekran arka planı** üzerinden yap; "oda karardı→uyku" fikri de LDR'siz kurulmalı.

### F7) Sadık ayna — terminale bakmayı bırak
Spinner *kelime havuzu* eşitli ama canlı sayaç yok. Elapsed süre + token/maliyet + "esc to interrupt" ipucunu `/status`'a ekleyip cihazda göster → terminale bakmadan glance-able harici durum çubuğu.

---

## Köprü mimarisi

```
Claude Code
   │  hooks → stdin'e JSON
   ▼
küçük local script (Python/Node)  ──serial veya WebSocket──►  ESP32 (CYD)
   │  olayı normalize edip yollar                              clawd: anim + ses + LED + dokunma
   ▲                                                            │
   └───────────  dokunmatik cevap (izin: allow/deny)  ◄────────┘
```

- **v1:** Cihaz zaten USB-C ile aynı makinede → **seri port** üzerinden konuş (WiFi kurulumu yok, gecikme minimum).
- **v2:** WebSocket'e geç, kablosuz tak.

> A ve B'deki her şey doğrudan **gerçek hook event'lerinden** gelir, tahmin yok. Sadece prompt'a göre sentiment/keyword tepkileri biraz heuristik.

---

## Önerilen yol haritası

1. **Olay protokolü** — hook JSON → cihaza giden kompakt mesaj formatı (beynin omurgası).
2. **Hook + köprü script'i** — birkaç olayı (`PreToolUse`, `Stop`) cihaza basıp seri porttan doğrula.
3. **Cihaz tarafı** — LVGL ile basit clawd + 2-3 state, gelen mesaja göre animasyon değişir.
4. **İzin ver/reddet dokunmatik akışı** (killer feature).
5. **Ses + LED + LDR + skinler** ile zenginleştirme.

---

## Açık sorular

- clawd görseli: hazır sprite/art seti var mı, yoksa birkaç ifadeli basit clawd yüzünü sıfırdan mı çizelim?
- Cihaz hep USB ile aynı makinede mi (seri yeterli) yoksa kablosuz/WiFi mı?
