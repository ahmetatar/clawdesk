# 01 — Hello World

The first project: drawing text on the CYD / ESP32-2432S028R display. This is
**step 3.2 ("first light")** from the guide. No clawd logic at all; the only
question is: **does the board program, and does the display come up?**

## Run

```bash
cd examples/01-hello-world

# build
pio run

# upload and open the serial monitor
pio run -t upload && pio device monitor
```

> If you don't have `pio`: install VS Code + the PlatformIO extension, or
> `pip install platformio`.

## Success criteria

- A green **"Hello, clawd!"** on screen with white explanatory text below it.
- A red dot blinking twice a second in the top-right corner (heartbeat).
- `[clawd] display: 240 x 320` and `heartbeat on/off` in the serial monitor
  (115200 baud).

## Troubleshooting

| Symptom | Where to look first |
|---|---|
| Screen entirely **white or black**, no text | the `TFT_*` pins in `platformio.ini` — your board revision may differ |
| Text appears but the **colours are swapped** (green↔red) | uncomment `-D TFT_RGB_ORDER=TFT_BGR` |
| The image looks **inverted / washed out** | uncomment `-D TFT_INVERSION_ON=1` |
| The screen is **sideways or upside down** | try `tft.setRotation(0)` → 1/2/3 in `main.cpp` |
| No port found / upload fails | check `ls /dev/cu.*`; you may need the CH340/CP2102 driver |
| The backlight stays off | verify `-D TFT_BL=21` and `-D TFT_BACKLIGHT_ON=HIGH` |

## Next step

If this worked, you have cleared the hardest trap. Next under `examples/`:
`02-blink-rgb` (RGB LED), `03-ldr` (light sensor), `04-touch` (touch + calibration).
