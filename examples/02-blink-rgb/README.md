# 02 — Blink RGB

Drives the CYD's **on-board** RGB LED. This is **step 4.1** from the guide, and the
basis of clawd's "mood light": breathing while thinking, green when tests pass, red
on an error.

## Wiring

**Nothing to wire.** The RGB LED is on the board (usually a small LED near the USB
connector). Just plug in USB.

| Colour | GPIO |
|---|---|
| Red | 4 |
| Green | 16 |
| Blue | 17 |

> **active-low:** writing `LOW` lights the pin, `HIGH` turns it off. Counterintuitive,
> but that's how the CYD is wired (the LED's common leg goes to 3.3V).

## Run

```bash
cd examples/02-blink-rgb
pio run -t upload
```

## Success criteria

The LED cycles **red → green → blue → yellow → cyan → magenta → white → off**, then
repeats, with a `[clawd] LED -> ...` line per colour in the serial monitor.

## Known hardware note for this unit

On this board the RGB LED's **red channel (GPIO4) does not light**; green (16) and
blue (17) are fine. Isolated testing (GPIO4 alone) produced no light at all, so the
red sub-LED appears faulty (cold solder joint or a dead die). The pin mapping and the
active-low logic are correct, as green and blue prove. clawd's mood light will
therefore be built without red (green/blue/cyan).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| The LED never lights | The pins may differ on your revision; verify R/G/B = 4/16/17 |
| The colours are **inverted** (dark on LOW) | Not active-low; swap LOW↔HIGH in `setRGB` |
| The wrong colour lights (blue when you asked for red) | The pin mapping is crossed; fix the PIN_R/G/B order |
| It stays lit and never changes | `loop()` may not be running; check the serial monitor |

## Next step

`03-ldr` — reading the light sensor (LDR) and the "room went dark → sleep" threshold.
