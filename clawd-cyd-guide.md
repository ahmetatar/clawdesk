# CYD (ESP32-2432S028R) — getting to know the board

> A hands-on roadmap for learning the board that is clawd's physical brain. This is
> not the clawd firmware yet; first we **get to know the board and make every part
> say hello, one at a time**.

---

## 0) What is this board? (the CYD legend)

**CYD = "Cheap Yellow Display"** — the nickname the community gave it. Its official
name is **ESP32-2432S028R**: a cheap ESP32 development board that ships with a
colour touchscreen attached. The numbers in the name mean something:

- **2432** → a 240×320 pixel display
- **S028** → 2.8 inches
- **R** → resistive touch panel

Why is it so popular? Because a display, touch, an ESP32, USB, an SD slot, a light
sensor, an LED and audio output all arrive **on one board, with no soldering**. Ideal
for clawd: we bring clawd's face to life on the screen, grant permissions by
touching it, and let the LED say "I'm thinking".

---

## 1) The hardware, piece by piece

### 1.1 The brain — ESP32 (ESP32-D0WD / WROOM-32)
- **Dual-core** Xtensa LX6 at ~240 MHz.
- **WiFi (2.4 GHz) + Bluetooth/BLE** built in — this WiFi carries the `clawd.local`
  mDNS webhook of the clawd protocol.
- **~520 KB SRAM** plus **4 MB flash** (firmware + the LittleFS animation store fit).
- Dual-core matters here: one core can run WiFi/HTTP while the other draws the
  screen (the "render in a separate task" decision in the architecture doc).

### 1.2 Display — 2.8" TFT, ILI9341 driver, 240×320, SPI
- clawd's **face and animation canvas**.
- The driver chip is **ILI9341**, a first-class citizen for the TFT_eSPI library.
- Driven over **SPI**, 16-bit colour (65K colours, RGB565).
- ⚠️ **The most common snag:** getting TFT_eSPI's pin definitions right via
  `build_flags` on this board. Get them wrong and the screen comes up white,
  mirrored, or with broken colours. (Step 3 has a working recipe.)

### 1.3 Touch — XPT2046 (resistive)
- Works by **pressing** rather than capacitively, so fingernails and gloves register.
- In clawd: **tap its head = "allow"**, swipe = "cancel" — the killer feature.
- ⚠️ **Gotcha:** on most CYDs the XPT2046 sits on a **separate SPI bus** from the
  display, so the code needs its own `SPIClass` instance for touch. Assume one
  shared bus and touch simply will not read.

### 1.4 RGB LED (one, on-board)
- A single RGB LED on the board — clawd's **mood light**: breathing while thinking,
  green when a test passes, red on an error.
- ⚠️ Usually **active-low** (LOW lights it, HIGH turns it off — counterintuitive).
- Likely pins (verify): **R=GPIO4, G=GPIO16, B=GPIO17.**

### 1.5 LDR — light sensor
- A photoresistor reading ambient light as an analog value; in clawd it drives
  **"the room went dark → sleep mode"**.
- Likely pin: **GPIO34** (an input-only ADC pin), read with `analogRead()`.

### 1.6 Audio — small speaker / buzzer output
- For Tamagotchi-style chirps, driven from the ESP32's **DAC** output.
- Likely pin: **GPIO26** (DAC1). A `tone()`-style call or a DAC waveform works.

### 1.7 microSD slot
- Plenty of storage for animation frames, sounds and alternative clawd skins.
- ⚠️ **Important trap:** the SD card shares **the same SPI bus as the TFT**, and
  driving both at once risks bus collisions. That is why the architecture says
  **"start with LittleFS (internal flash) rather than SD"** — fewer surprises.

### 1.8 USB and power
- **USB** (USB-C or micro-USB depending on the board) provides power, programming
  and serial communication. There is a **CH340** or **CP2102** USB-serial converter
  on board, which may need a driver (step 2).
- Powered from 5V USB; the 3.3V regulator is on the board.

### 1.9 Spare GPIO connectors (JST)
- A few free pin headers along the edges (I2C / general GPIO). An **I2S microphone**
  (for a future "clawd, what are you doing?" feature) could go here.

### Pin cheat-sheet (verify before writing any code)

| Part | Chip | Pin(s) | Note |
|---|---|---|---|
| TFT display | ILI9341 | SPI (SCK/MOSI/MISO/CS/DC/RST/BL) | set via build_flags |
| Touch | XPT2046 | **separate SPI** + IRQ | needs its own SPIClass |
| RGB LED | — | R=4, G=16, B=17 | **active-low** |
| LDR | — | 34 | input-only, ADC |
| Buzzer/audio | DAC | 26 | DAC1 |
| SD card | — | SPI (shared with the TFT) | watch for collisions |

> ⚠️ The CYD has several hardware revisions, so the pins may differ on **your**
> board. Step 4 verifies them with a pin-scanning sketch. Don't trust the guess,
> measure it.

---

## 2) Environment setup (once, before any code)

1. **Install PlatformIO.** VS Code plus the PlatformIO extension (more comfortable
   than the Arduino IDE, and the architecture is built on PlatformIO anyway).
2. **USB-serial driver.** Plug the board in; if it does not appear on macOS, install
   the CH340 or CP2102 driver. Verify the port in a terminal:
   ```bash
   ls /dev/tty.* /dev/cu.*      # on macOS it looks like /dev/cu.usbserial-XXXX
   ```
3. **Open an empty PlatformIO project** (board: `esp32dev`). Not the clawd firmware
   yet — just experiments to learn the board.
4. **Get into the serial monitor habit:** 115200 baud. Every experiment prints what
   is happening with `Serial.println()`. It is your eyes and ears.

---

## 3) "First light" — see the board come alive

> Goal: no clawd logic at all. Just "does the board program, does the screen light up?"

**3.1 Blink (light the LED).** The first touch of the hardware: blink one leg of the
RGB LED. This is where active-low becomes tangible (LOW = lit). Success: the LED
blinks and "blink" appears in the serial monitor.

**3.2 Bring up the display (TFT_eSPI "hello").** The critical step — the pin recipe.
Add TFT_eSPI to `platformio.ini` and give it the CYD pins via `build_flags`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps = bodmer/TFT_eSPI
build_flags =
  -D USER_SETUP_LOADED=1
  -D ILI9341_2_DRIVER=1
  -D TFT_WIDTH=240
  -D TFT_HEIGHT=320
  -D TFT_MISO=12
  -D TFT_MOSI=13
  -D TFT_SCLK=14
  -D TFT_CS=15
  -D TFT_DC=2
  -D TFT_RST=-1
  -D TFT_BL=21
  -D TFT_BACKLIGHT_ON=HIGH
  -D SPI_FREQUENCY=55000000
  -D LOAD_GLCD=1
```
> These pins are typical for a CYD but **may differ on your board** — if the screen
> comes up mirrored or white, suspect this first. If the colours are inverted, try
> the `TFT_RGB_ORDER` or `TFT_INVERSION_ON/OFF` flags.

In the code: clear the screen with `tft.fillScreen(TFT_BLACK)` and draw
`tft.drawString("clawd")` in the middle. Success: you can read the text on screen.
**Get past this and you have cleared the hardest trap.**

**3.3 Colour test.** Fill the screen with red, green and blue. Correct colours mean
the pin order and RGB order are right.

---

## 4) Make every sensor talk (hardware inventory)

> Goal: prove which pin each part in the cheat-sheet **actually** uses on your board.
> One small sketch each, each printing to the serial monitor.

1. **RGB LED — each colour separately.** Light R, G and B in turn to confirm the pins
   and the active-low behaviour. This is the basis of clawd's mood light.
2. **LDR — measure the light.** Print `analogRead(34)` in a loop and cover the sensor
   with your hand; the value must change. Note the dark/light threshold for sleep mode.
3. **Touch (XPT2046) — the most important exercise.** Initialise touch with its own
   `SPIClass` and print the (x, y) you press. Press all four corners and note the raw
   values for **calibration** (screen pixel ↔ raw touch value). This is the heart of
   clawd's "tap its head = allow" feature.
4. **Audio — beep.** Emit a simple tone on GPIO26. Does the speaker make a sound?
   This is the basis of clawd's "ta-da" and "ugh" noises.
5. **(Optional) SD card.** Insert a card and print the file list. Observe the bus
   collision with the TFT here — which is why LittleFS comes first.

At the end of this step you have **a verified pin map for your own board**, and you
write the clawd firmware against that map rather than against a guess.

---

## 5) Combine the hardware — a mini interaction

> Bring the individually working parts together for the first time. Still not clawd;
> call it a hardware orchestra.

- **Touch → the screen responds.** Draw a dot where you press (touch + display).
- **Touch → LED + sound.** A touch lights the LED green and emits a short beep
  (three parts at once).
- **Dark → the screen dims.** Below the LDR threshold, dim the backlight or put the
  screen to sleep (sensor → behaviour).

Once you are here you control **all of the board's I/O** and are ready to dress it up
as clawd.

---

## 6) WiFi and networking — the bridge to the clawd protocol

> Having learned the hardware, try the protocol's transport layer.

1. **Join WiFi** (hard-coded SSID/password first, then a WiFiManager captive portal).
2. **Become `clawd.local`** via mDNS (`MDNS.begin("clawd")`).
3. **Set up ESPAsyncWebServer** with a single `GET /health` endpoint returning
   `{"fw":"0.1.0"}`.
4. Try it from the PC:
   ```bash
   curl http://clawd.local/health
   ```
   A response means the **PC ↔ device bridge is up**. The protocol's other endpoints
   (`POST /e`, `POST /perm`) build on this.

---

## 7) From here — becoming clawd

With the hardware and networking understood, you join the real roadmap in the plan
documents:

1. **The event protocol** (`clawd-device-protocol.md`) — hook JSON → device message.
2. **The hook + bridge script** — push Claude Code events to `clawd.local`.
3. **The device side** — a simple clawd face plus a few states (`thinking`,
   `hacking`, `oops`, …).
4. **The allow/deny touch flow** — the payoff of the calibration in step 4.3.
5. **Enrichment** with sound, LED, LDR and skins.

---

## Checklist (work through it in order)

- [ ] PlatformIO + USB-serial driver installed, port visible
- [ ] Blink works (LED + serial monitor)
- [ ] **Text visible on the screen** (the hardest trap cleared)
- [ ] Colour test correct (RGB order right)
- [ ] RGB LED verified per colour, active-low confirmed
- [ ] LDR value changes by hand, threshold noted
- [ ] **Touch (x,y) reads, all four corners calibrated**
- [ ] The buzzer makes a sound
- [ ] Touch → screen + LED + sound respond together
- [ ] WiFi + mDNS + `/health` → `curl http://clawd.local/health` responds
- [ ] **A verified pin map for my own board** in hand

> The two starred (**) steps are the thresholds that gate the rest of the project:
> bringing up the display and calibrating touch. Past those, the rest is charm.
