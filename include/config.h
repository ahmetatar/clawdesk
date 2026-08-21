#pragma once
// clawd — all tunable constants in one place.

// ---- pins ----
constexpr int PIN_BL   = 21;   // backlight (driven by LEDC PWM)
constexpr int LED_G    = 16;   // RGB green (active-low)
constexpr int LED_B    = 17;   // RGB blue  (active-low) — red (GPIO4) is dead on this board
constexpr int T_CLK    = 25;   // touch (HSPI)
constexpr int T_CS     = 33;
constexpr int T_MOSI   = 32;
constexpr int T_MISO   = 39;
constexpr int PIN_SPEAKER = 26; // 3.5mm jack — direct DAC/PWM out, no onboard amp

// ---- display / animation ----
constexpr int ANIM_W   = 64;   // every animation is 64x64
constexpr int ANIM_H   = 64;
constexpr int ANIM_S   = 3;    // scale: 64*3 = 192 px
constexpr int HOLD_MS  = 4000; // transient moods (happy/oops) return to idle after this

// ---- background (letterbox + animation frame background) ----
// Smoky black, not pure black — makes the orange clawd pop.
// Must match BG in tools/pixellab/{03_png_to_header.py, clawd_anim.py}; if you
// change it, regenerate the headers and copy them into src/anims/.
constexpr uint8_t BG_R = 36;
constexpr uint8_t BG_G = 39;
constexpr uint8_t BG_B = 44;

// 8-8-8 -> RGB565 (tft.color565 is a member function, not constexpr).
#ifndef RGB565
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#endif

// clawd's dominant orange — HUD action text always uses it, regardless of mood.
constexpr uint16_t CLAWD_ORANGE = RGB565(213, 82, 56);

// ---- WiFi ----
// On a failed connect, count down this many seconds on screen and ESP.restart().
constexpr int WIFI_RETRY_SECS = 10;

// ---- power management ----
// idle = time since the last event or touch.
constexpr uint32_t T_DIM_MS   = 30000;  // dim the backlight
constexpr uint32_t T_SLEEP_MS = 120000; // blank the screen and sleep

// While Claude is busy (events started, no Stop yet) dim/sleep is disabled so a
// long build/test never puts the device to sleep. Safety cap in case Stop never
// arrives — must exceed the longest realistic tool run.
constexpr uint32_t T_BUSY_MAX_MS = 600000;  // 10 min

// Above this context usage, an idle clawd switches to ANIM_BRAIN_FULL as a
// warning layer (same "red" threshold the statusLine uses).
constexpr int CTX_BRAIN_THRESH = 80;

// backlight levels (8-bit LEDC duty, 0..255)
constexpr uint8_t  BL_FULL = 255;
constexpr uint8_t  BL_DIM  = 28;        // ~11%: dim but still readable
constexpr uint8_t  BL_OFF  = 0;

// LEDC (hardware PWM) — backlight
constexpr int      BL_CH   = 0;
constexpr int      BL_FREQ = 20000;     // 20 kHz: above audible whine
constexpr int      BL_RES  = 8;         // 8-bit (0..255)

// LEDC (hardware PWM) — speaker tone, separate channel from the backlight
constexpr int      SPK_CH  = 1;
constexpr int      SPK_RES = 10;        // 10-bit duty (only used at 50% for a square wave)

// soft fade: duty moves BL_STEP every BL_RAMP_MS (~170 ms for a full fade)
constexpr uint8_t  BL_STEP     = 12;
constexpr uint32_t BL_RAMP_MS  = 8;

// sleep CPU frequency (80 MHz is the minimum safe for WiFi, ~half the power)
constexpr int      CPU_HZ_ACTIVE = 240;
constexpr int      CPU_HZ_SLEEP  = 80;

// ---- touch gestures (tickle / pet) ----
// XPT2046 getPoint() returns RAW ADC (~200..3900), so these thresholds are in
// raw units. Double-tap = two short taps; petting = one contact with a wide
// horizontal sweep.
constexpr uint32_t TAP_MAX_MS       = 350;  // short contact with little movement = tap
constexpr uint32_t DOUBLETAP_MS     = 500;  // gap below this between taps = double-tap
constexpr int      STROKE_MIN_RAW   = 400;  // x travel in one contact = stroke
constexpr uint32_t TOUCH_SETTLE_MS  = 25;   // skip ADC noise at contact start
constexpr uint32_t TOUCH_RELEASE_MS = 55;   // no contact this long = real release

// ---- touch calibration (raw ADC -> screen px, rotation 1) ----
// Verified against the four corners in example 04-touch. Only needs to be
// roughly right — the corner button zone is generous.
constexpr int RAW_X_MIN = 1800, RAW_X_MAX = 3300;
constexpr int RAW_Y_MIN = 1600, RAW_Y_MAX = 3300;

// ---- view switcher (top-right corner button) ----
// A short touch STARTING in the top-right corner toggles mascot <-> usage view.
// Contacts that start there never enter the gesture machine.
constexpr int      UI_BTN_W        = 90;   // touch zone width (from the right edge)
constexpr int      UI_BTN_H        = 56;   // height (from the top)
constexpr uint32_t UI_TOGGLE_DEBOUNCE_MS = 450;

// ---- usage (quota) view ----
// Clock + two cards (Current/Weekly) + progress bar + "Resets in ..." + spinner.
constexpr uint16_t UI_CARD_BG    = RGB565(56, 56, 63);    // card background
constexpr uint16_t UI_PILL_BG    = RGB565(92, 87, 110);   // "Current/Weekly" badge
constexpr uint16_t UI_BAR_TRACK  = RGB565(74, 68, 94);
constexpr uint16_t UI_BAR_FILL   = RGB565(238, 166, 100);
constexpr uint16_t UI_TEXT_MAIN  = RGB565(255, 255, 255); // percentages + clock
constexpr uint16_t UI_TEXT_SOFT  = RGB565(222, 222, 228); // "Resets in ..."

// NTP clock (the big clock on the usage view). Istanbul: UTC+3, no DST.
constexpr long CLOCK_TZ_OFFSET_S = 3 * 3600;

// ---- network / static IP ----
// The device sits behind a range extender, so the main router never sees its
// real MAC and a DHCP reservation would not stick — the device pins its own IP
// instead. Keep it outside the router's DHCP pool to avoid collisions, and set
// CLAWD_HOST (PC hook + statusLine) to the same address.
// Set CLAWD_STATIC_IP to 0 to fall back to DHCP.
//
// install.sh rewrites these lines automatically from the detected network.
#define CLAWD_STATIC_IP 1
constexpr uint8_t IP_LOCAL[4]   = {192, 168, 1, 201};
constexpr uint8_t IP_GATEWAY[4] = {192, 168, 1, 1};
constexpr uint8_t IP_SUBNET[4]  = {255, 255, 255, 0};
constexpr uint8_t IP_DNS[4]     = {192, 168, 1, 1};
