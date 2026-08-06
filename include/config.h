#pragma once
// clawd ana uygulama — tum ayarlanabilir sabitler tek yerde.

// ---- pinler ----
constexpr int PIN_BL   = 21;   // arka isik (LEDC PWM ile suruyoruz)
constexpr int LED_G    = 16;   // RGB yesil (active-low)
constexpr int LED_B    = 17;   // RGB mavi  (active-low)  — kirmizi (GPIO4) bu kartta olu
constexpr int T_CLK    = 25;   // dokunmatik (HSPI)
constexpr int T_CS     = 33;
constexpr int T_MOSI   = 32;
constexpr int T_MISO   = 39;

// ---- ekran / animasyon ----
constexpr int ANIM_W   = 64;   // tum animasyonlar 64x64
constexpr int ANIM_H   = 64;
constexpr int ANIM_S   = 3;    // olcek: 64*3 = 192 px
constexpr int HOLD_MS  = 4000; // gecici ifadeler (happy/oops) sonrasi idle'a donus

// ---- zemin rengi (letterbox + animasyon frame arka plani) ----
// FUME SIYAH: tam siyah degil, hafif serin koyu kul. Turuncu clawd'i one cikarir.
// DIKKAT: tools/pixellab/{03_png_to_header.py, clawd_anim.py} BG ile AYNI olmali,
// yoksa fume letterbox uzerinde eski renkte kare kalir. Degistirince header'lari
// yeniden uret (python3 03_png_to_header.py out/anim_<ad> clawd_<ad>) + src/anims'e kopyala.
constexpr uint8_t BG_R = 36;
constexpr uint8_t BG_G = 39;
constexpr uint8_t BG_B = 44;

// 8-8-8 -> RGB565 (statik baslatma icin; tft.color565 uye fonksiyon, constexpr degil).
#ifndef RGB565
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#endif

// clawd maskotunun baskin turuncusu (out/clawd_south.png'den orneklendi) — HUD
// aksiyon metni bu sabit renkte yazilir (mood'a gore degil).
constexpr uint16_t CLAWD_ORANGE = RGB565(213, 82, 56);

// ---- WiFi ----
// connectWiFi basarisiz olursa ekranda bu kadar saniye geri sayilir, sonra ESP.restart()
// ile bastan denenir (router gec acilmis olabilir). Kalici sorunda (yanlis sifre) dongude
// kalir; kullanici install.sh ile duzeltip yeniden flaslar.
constexpr int WIFI_RETRY_SECS = 10;

// ---- guc yonetimi ----
// idle = son event/dokunmadan bu yana gecen sure.
constexpr uint32_t T_DIM_MS   = 30000;  // 30 sn: arka isigi kis
constexpr uint32_t T_SLEEP_MS = 120000; // 120 sn: ekrani sondur + uyku

// Claude MESGULKEN (olay akisi basladi, Stop gelmedi) uyku/kisma TAMAMEN kapali:
// uzun bir tool (build/test) calisirken cihaz uyumaz. GUVENLIK: Stop olayi hic
// gelmezse bu kadar OLAYSIZ sureden sonra normal idle'a don (ekran takili kalmasin).
// Uzun tool'lardan buyuk olmali (build/test'ler dakikalar surebilir).
constexpr uint32_t T_BUSY_MAX_MS = 600000;  // 10 dk emniyet tavani

// context (ctx%) bu esigi (statusLine ile AYNI "kirmizi" esik) gecince, aktif-bosta
// (ANIM_IDLE) iken clawd baglam-doluluk gostergesine (ANIM_BRAIN_FULL) gecer — DIM/
// SLEEP'e gecmeden ONCE bir uyari katmani. Esik altina donunce idle'a geri doner.
constexpr int CTX_BRAIN_THRESH = 80;

// arka isik parlaklik seviyeleri (8-bit LEDC duty, 0..255)
constexpr uint8_t  BL_FULL = 255;       // aktif: tam parlaklik
constexpr uint8_t  BL_DIM  = 28;        // ~%11: kisik ama okunur/canli
constexpr uint8_t  BL_OFF  = 0;         // uyku: kapali

// LEDC (donanim PWM) — arka isik
constexpr int      BL_CH   = 0;         // LEDC kanali
constexpr int      BL_FREQ = 20000;     // 20 kHz: duyulabilir cizirti olmaz
constexpr int      BL_RES  = 8;         // 8-bit cozunurluk (0..255)

// yumusak fade: her adimda duty bu kadar degisir, RAMP_MS periyotla
constexpr uint8_t  BL_STEP     = 12;
constexpr uint32_t BL_RAMP_MS  = 8;     // ~ (255/12)*8 ≈ 170 ms tam fade

// uyku CPU frekansi (240 -> 80 MHz: WiFi icin min guvenli, ~yari guc)
constexpr int      CPU_HZ_ACTIVE = 240;
constexpr int      CPU_HZ_SLEEP  = 80;

// ---- dokunmatik jestleri (gidiklama / oksama) ----
// XPT2046 getPoint() HAM ADC dondurur (~200..3900). Cift-dokunus = iki kisa tap;
// surtme/oksama = tek dokunusta genis yatay x hareketi. Esikler ham ADC birimidir.
constexpr uint32_t TAP_MAX_MS       = 350;  // tek temas bundan kisaysa (+az hareket) = "tap"
constexpr uint32_t DOUBLETAP_MS     = 500;  // iki tap arasi bundan az ise = cift-tap (gidiklama)
constexpr int      STROKE_MIN_RAW   = 400;  // tek temasta x bu kadar degisirse = surtme (oksama)
constexpr uint32_t TOUCH_SETTLE_MS  = 25;   // temas basindaki ADC gurultusunu atla (tap araligi sismesin)
constexpr uint32_t TOUCH_RELEASE_MS = 55;   // bu kadar temassizlik = GERCEK birakma. Kisa basinc
                                            // dususu drag'i bolmez; deliberate cift-tap (gap>100ms) birlesmez.

// ---- dokunmatik kalibrasyon (ham ADC -> ekran px, rotation 1) ----
// 04-touch orneginde 4 koseye dokunarak dogrulanan degerler. Kose-buton tespiti
// icin kabaca dogru olmasi yeter (zone genis tutulur).
constexpr int RAW_X_MIN = 1800, RAW_X_MAX = 3300;
constexpr int RAW_Y_MIN = 1600, RAW_Y_MAX = 3300;

// ---- gorunum degistirici (sag-ust kose butonu) ----
// Sag-ust kosede BASLAYAN kisa dokunus normal <-> usage (kota) ekranini degistirir.
// Kose'de baslayan temas jest makinesine (tickle/love) GIRMEZ.
constexpr int      UI_BTN_W        = 90;   // dokunma bolgesi genisligi (ekran px, sagdan)
constexpr int      UI_BTN_H        = 56;   // yuksekligi (ustten)
constexpr uint32_t UI_TOGGLE_DEBOUNCE_MS = 450;

// ---- usage (kota) ekrani ----
// Ekran goruntusundeki Claude kota gorunumu: saat + 2 kart (Current/Weekly)
// + progress bar + "Resets in ..." + altta spinner kelimesi.
constexpr uint16_t UI_CARD_BG    = RGB565(56, 56, 63);    // kart zemini (koyu gri)
constexpr uint16_t UI_PILL_BG    = RGB565(92, 87, 110);   // "Current/Weekly" rozeti (mor-gri)
constexpr uint16_t UI_BAR_TRACK  = RGB565(74, 68, 94);    // bar yatagi (mor)
constexpr uint16_t UI_BAR_FILL   = RGB565(238, 166, 100); // bar dolgusu (acik turuncu)
constexpr uint16_t UI_TEXT_MAIN  = RGB565(255, 255, 255); // %'ler + saat (beyaz)
constexpr uint16_t UI_TEXT_SOFT  = RGB565(222, 222, 228); // "Resets in ..." (kirik beyaz)

// NTP saat (usage ekranindaki buyuk saat). Istanbul: UTC+3, DST yok.
constexpr long CLOCK_TZ_OFFSET_S = 3 * 3600;

// ---- ag / statik IP ----
// Bu cihaz bir TP-Link menzil genisletici (TL-WA854RE) ARKASINDA calisiyor;
// ana router (192.168.1.1) clawd'in gercek MAC'ini GORMEZ (extender proxy ARP
// yapar) -> router tarafinda DHCP reservation ISE YARAMAZ. Cozum: cihaz IP'yi
// KENDISI sabitler. Secilen IP ana router'in DHCP havuzunun (192.168.1.100-.200)
// DISINDA olmali ki router baskasina dagitip cakismasin. .201 = havuz disi, ayni
// LAN /24. CLAWD_HOST (PC hook + statusLine) bu IP'ye ayarlanmali.
// BASKA bir agda kullanacaksan CLAWD_STATIC_IP'yi 0 yap -> normal DHCP'ye doner.
//
// NOT: install.sh bu satiri OTOMATIK yeniden yazar — /24'un son adresini onerir
// (192.168.1.254) ve Enter'a basilirsa onu yazar. Buradaki .201'i korumak icin
// kurulum sorusunda IP'yi ELLE .201 girmek gerekir.
#define CLAWD_STATIC_IP 1
constexpr uint8_t IP_LOCAL[4]   = {192, 168, 1, 201};
constexpr uint8_t IP_GATEWAY[4] = {192, 168, 1, 1};
constexpr uint8_t IP_SUBNET[4]  = {255, 255, 255, 0};
constexpr uint8_t IP_DNS[4]     = {192, 168, 1, 1};
