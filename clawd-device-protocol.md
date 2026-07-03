# clawd Cihaz Protokolü

> Claude Code (PC) ↔ clawd cihazı (ESP32/CYD) arasındaki olay protokolü.
> **Taşıma: HTTP webhook + mDNS (WiFi).** Tek versiyon, tek mimari.

## Tasarım ilkeleri

1. **Anlam taşı, animasyon değil.** PC "ne olduğunu" bildirir; hangi animasyon/ses/LED'in oynayacağına **cihaz** karar verir. Firmware ile PC bağımsız evrilir.
2. **Ağır iş PC'de.** Tool gruplama, git tespiti, süre/sentiment hesabı hook script'lerinde. ESP32 sadece `kind → state` eşler.
3. **Daemon yok.** Claude Code hook'ları kısa ömürlüdür: tetiklenir, cihaza bir HTTP isteği atar, çıkar. Uzun süre çalışan bir köprü süreci gerekmez.

---

## 1) Mimari

```
Claude Code hook (kısa ömürlü)  ──HTTP──►  ESP32 HTTP sunucusu (clawd.local)
   curl / minik POST                       olayı render eder: anim + ses + LED
                                           dokunmatik → izin cevabı
```

- ESP32 WiFi'ye bağlanır, üstünde **ESPAsyncWebServer** koşar.
- **mDNS** ile kendini `clawd.local` olarak duyurur (`MDNS.begin("clawd")`) — IP hardcode edilmez.
- İlk WiFi kurulumu **WiFiManager** captive portal ile bir kez yapılır.
- Her hook olayı cihazın bir ucuna POST'lar; HTTP body = envelope JSON'u.

---

## 2) Uçlar (endpoints)

| Uç | Yön | Ne yapar |
|---|---|---|
| `POST /e` | hook → cihaz | Fire-and-forget olay. Body = envelope. **204** döner. |
| `POST /status` | statusLine → cihaz | HUD üst bandı özeti `{m,ctx,h5,wk}`. **204**. Güç yönetimine dokunmaz. |
| `POST /words` | komut → cihaz | Spinner kelime havuzunu değiştir `{"w":["Cogitating",…]}`. **200** `{ok,n}` / boş→**400**. RAM'de tutulur, reboot'ta `spinner_words.h` varsayılanına döner. |
| `POST /perm` | hook → cihaz | İzin sorusu. Body `{"id":7,"d":{tool,s,risk}}`. Hemen `{"pending":true}` döner, ekranda prompt açılır. |
| `GET /perm/{id}` | hook → cihaz | Karar yoklaması. `{"decision":"allow"\|"deny"}` ya da `{"pending":true}`. |
| `GET /health` | hook → cihaz | Canlılık + cihaz bilgisi: `{"fw":"0.1.0","caps":["led","audio","touch","ldr"]}`. |

> Cihaz→PC kendiliğinden push (örn. `touch.pet`) **yok**: bu olaylar Claude Code'u etkilemez, sadece cihaz içi sevimliliktir. İzin cevabı da `GET /perm/{id}` ile çözülür. Bu yüzden PC tarafında dinleyici/daemon gerekmez.

---

## 3) Envelope

POST `/e` gövdesi:

```json
{"k":"tool.pre","d":{ }}
```

| Alan | Zorunlu | Açıklama |
|---|---|---|
| `k` | ✓ | kind — noktalı isim alanı (`tool.pre`, `git`, `think`...) |
| `d` | kind'a göre | payload; kısa anahtarlar |

`/perm` gövdesi ek olarak `id` taşır (korelasyon için PC üretir):

```json
{"id":7,"d":{"tool":"Bash","s":"rm -rf build","risk":"high"}}
```

HTTP zaten çerçeveleme/yön/tip bilgisini taşıdığı için seri porttaki `t` (mesaj tipi) ve satır sınırlayıcı (`\n`) alanlarına gerek yok.

---

## 4) Olay kataloğu (`POST /e`)

| `k` | `d` payload | Tetikleyici hook / anlam |
|---|---|---|
| `session.start` | `{cwd, model}` | `SessionStart` |
| `session.end` | `{tools, edits, commits, ms}` | `SessionEnd` — günün özeti |
| `prompt.submit` | `{len, mood}` | `UserPromptSubmit` (mood: neutral/fix/big/frustrated) |
| `think` | `{on}` | düşünme başladı/bitti → nefes LED'i |
| `tool.pre` | `{g, tool, s}` | `PreToolUse` (g=grup, s=özet) |
| `tool.post` | `{g, ok, ms}` | `PostToolUse` (ok=başarı, ms=süre) |
| `agent.spawn` | `{n}` | `Task`/workflow |
| `agent.stop` | `{}` | `SubagentStop` |
| `git` | `{op}` | op: commit/push (Bash input'undan) |
| `notify` | `{kind, s}` | `Notification` (kind: idle/info) |
| `compact` | `{}` | `PreCompact` |
| `wait` | `{on}` | `Stop` sonrası bekleme |
| `status` | `{ctx, cost}` | statusline'dan periyodik (ctx=0-100 context%) — aynı zamanda heartbeat |

### Tool grupları (`g`) — hook tarafı normalizasyon

| grup | Claude Code araçları |
|---|---|
| `exec` | Bash |
| `edit` | Edit, Write, MultiEdit, NotebookEdit |
| `read` | Read |
| `search` | Grep, Glob |
| `web` | WebFetch, WebSearch |
| `agent` | Task, Workflow |
| `ext` | mcp__* |
| `plan` | TaskCreate/Update, TodoWrite |

---

## 5) İzin akışı (HTTP — poll tabanlı)

`PreToolUse` hook'u senkron bloklar; kararı dokunmatikten yoklayarak bekler. Bağlantıyı tutmak yerine yoklamak ESP32'de watchdog/timeout açısından daha sağlamdır.

```
hook  ──POST /perm {id:7, "rm -rf build", risk:high}──►  cihaz: prompt göster → {pending:true}
hook  ──GET  /perm/7   (250 ms'de bir, ~10 sn)────────►  cihaz: {pending:true} ...
        kullanıcı: clawd'a dokun = allow / kaydır = deny
hook  ──GET  /perm/7──────────────────────────────────►  cihaz: {decision:"allow"}
hook  → allow/deny döner, aracı serbest bırakır
```

- `risk`: low/med/high → cihaz renk/aciliyet ayarlar.
- **Timeout hook'ta** (öneri ~10 sn). Cevap gelmezse hook `ask`'e düşer → normal Claude Code izin ekranı (kilitlenme yok).
- `id` eşleştirmesi eşzamanlı izinleri ayırır. Cihaz cevabı verdikten sonra `id`'yi kısa süre tutar (tekrar GET'e idempotent cevap).

---

## 6) Performans — hook'ları yavaşlatma

Her olay bir ağ çağrısıdır; her araca gecikme eklememek için:

- **İzin dışı tüm olaylar fire-and-forget**: minik timeout + arka plan, cevap bekleme. Cihaz erişilemese bile Claude Code takılmaz:
  ```bash
  curl -s --max-time 0.3 http://clawd.local/e -H 'content-type: application/json' -d @- >/dev/null 2>&1 &
  ```
- Sadece `perm.ask` bilinçli olarak bloklar (zaten bloklaması gereken tek olay).

---

## 7) Keşif, canlılık & güvenlik

- **mDNS**: cihaz `clawd.local`. Hook'lar bu isme atar, IP değişse de çalışır.
- **Canlılık**: cihaz son olay zamanını tutar; `status` POST'ları (statusline'dan periyodik) heartbeat görevi görür. **~15 sn** olaysızlıkta cihaz uyku/idle moduna geçer; yeni olayla uyanır.
- **Güvenlik (LAN)**: opsiyonel paylaşılan token. Hook `X-clawd-key: <token>` header'ı ekler; cihaz eşleşmeyen isteği 401 ile reddeder. Rastgele LAN POST'larını önler.
- **WiFi kurulumu**: WiFiManager captive portal — şifre bir kez girilir, NVS'e yazılır.

---

## 8) Tam örnek akış (HTTP)

```http
GET  /health                                  → 200 {"fw":"0.1.0","caps":["led","audio","touch","ldr"]}
POST /e   {"k":"session.start","d":{"cwd":"clawd-dashboard","model":"opus-4-8"}}   → 204
POST /e   {"k":"prompt.submit","d":{"len":142,"mood":"fix"}}                       → 204
POST /e   {"k":"think","d":{"on":true}}                                            → 204
POST /e   {"k":"tool.pre","d":{"g":"search","tool":"Grep","s":"TODO"}}             → 204
POST /e   {"k":"tool.post","d":{"g":"search","ok":true,"ms":120}}                  → 204
POST /perm {"id":1,"d":{"tool":"Bash","s":"git push","risk":"med"}}               → 200 {"pending":true}
GET  /perm/1                                  → 200 {"pending":true}
GET  /perm/1                                  → 200 {"decision":"allow"}
POST /e   {"k":"git","d":{"op":"push"}}                                            → 204
POST /e   {"k":"think","d":{"on":false}}                                           → 204
POST /e   {"k":"wait","d":{"on":true}}                                             → 204
```

---

## 9) Cihaz tarafı referans state machine (normatif değil)

Olaylar → clawd durumları için varsayılan eşleme (firmware'de değiştirilebilir):

| Olay | clawd durumu |
|---|---|
| `think on` | `thinking` (nefes LED) |
| `tool.pre g=exec` | `hacking` |
| `tool.pre g=edit` | `writing` |
| `tool.pre g=search/read` | `searching` |
| `tool.pre g=web` | `surfing` |
| `agent.spawn` | `cloning` (mini-clawd'lar) |
| `tool.post ok=false` | `oops` (facepalm) |
| `git op=commit/push` | `celebrate` / `rocket` |
| `compact` | `defrag` |
| `perm.ask` | `alert` (ekrana vurur) |
| `wait on` | `waiting` → uzun sürerse `sleeping` (LDR karanlıksa) |
| `notify idle` | `idle` |

---

## 10) Versiyonlama & genişletme

- Bilinmeyen `k` → cihaz sessizce yok sayar (ileri uyumluluk).
- Yeni alanlar `d` içine eklenir; eski firmware görmezden gelir.
- Kırıcı değişiklikte `/health` içindeki `fw` ile uyum kontrol edilir.

---

## 11) Açık kararlar

1. **Risk skoru kaynağı.** `perm.ask`'teki `risk`'i hook script'i basit bir desen tablosuyla hesaplasın (öneri): `rm -rf`, `git push --force`, `sudo`, `> /dev/...` → high; yazma/exec → med; salt-okur → low.
2. **`d` anahtar uzunluğu.** Şu an okunabilirlik için tam isimler (`tool`, `mood`) + kısaltmalar (`g`, `s`) karışık. ESP32'de ArduinoJson rahat kaldırır; istenirse tam tutarlılığa çekilir.

**Sıradaki adım:** hook script'leri + `/e` ve `/perm` uçlarını taşıyan ESP32 iskeleti; birkaç olayı gerçek Claude Code'dan `clawd.local`'e basıp `/health` ve seri monitörden doğrulamak.
