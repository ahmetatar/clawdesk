# 04 — Touch (XPT2046 + calibration)

Reads the CYD's resistive touchscreen (XPT2046), draws a dot where you press, and
prints the raw (x,y,z) values to the serial port. This is **step 4.3** from the guide
— the heart of clawd's "tap its head = allow" killer feature.

## The critical point: a separate SPI bus

On the CYD, touch sits on a **separate SPI bus** from the display, so the code opens
its own `SPIClass` instance and supplies the pins explicitly. Assume one shared bus
and touch will never read — the CYD's best-known trap.

| Signal | GPIO |
|---|---|
| T_CLK | 25 |
| T_CS | 33 |
| T_MOSI | 32 |
| T_MISO | 39 |
| T_IRQ | 36 |

## Run

```bash
cd examples/04-touch
pio run -t upload
```

## Success criteria

- A **green dot** appears under your finger when you touch the screen.
- A `[clawd] raw x=.. y=.. z=.. -> screen (..,..)` line per touch in the serial monitor.

## Calibration (four corners)

On the first upload the dot may be offset from your finger, or the axes may be
inverted. That's expected — the raw values still need mapping to screen pixels:

1. **Press each of the four corners** and read the raw `x` and `y` values from the
   serial monitor.
2. Put the smallest/largest raw `x` into `RAW_X_MIN/MAX` and raw `y` into
   `RAW_Y_MIN/MAX` in `main.cpp`.
3. Upload again; the dot should now follow your finger.

| Symptom | Fix |
|---|---|
| The dot appears but the **axes are inverted** (right press reads left) | Swap MIN↔MAX in `map()` |
| **X and Y are crossed** (horizontal movement goes vertical) | Adjust `setRotation`, or swap p.x↔p.y |
| Touch **never reads** | Verify the separate SPI pins (25/33/32/39) |
| Random dots appear constantly | Add a `p.z` (pressure) threshold and ignore low values |

## Next step

Touch + display + LED together → clawd's permission flow: tap = allow, swipe = deny.
From here, see sections 5-6 of `clawd-cyd-guide.md` (combining the hardware + the
WiFi bridge).
