# 03 — LDR (light sensor)

Reads the CYD's **on-board** LDR from GPIO34. This is **step 4.2** from the guide,
and the basis of clawd's "room went dark → sleep mode" feature.

## Wiring

**Nothing to wire.** The LDR is a small orange/brown component on the board. GPIO34
is an input-only ADC pin.

## Run

```bash
cd examples/03-ldr
pio run -t upload
```

Then open the serial monitor and **cover the sensor with your hand** or change the
room lighting, and watch the value move.

## Success criteria

- A continuous `[clawd] LDR=NNNN -> light/DARK` line in the serial monitor.
- The value changes noticeably when you cover the sensor.
- Once it is dark enough, the **blue LED lights** (the sleep indicator).

## Known hardware note for this unit

**The LDR on this board is missing or faulty.** GPIO34 never responded to light,
reading a constant ~2425. Either the LDR (GT36516) is not fitted or it is open, in
which case only the internal 1M+1M divider remains, giving a fixed mid-scale value.
This is a **known issue** on the ESP32-2432S028R (there are "LDR ... not working"
threads on the forums). This board's red LED is faulty too.

It is not a blocker for clawd: sleep mode runs off an **inactivity timer** instead.
If you want one, wire an external LDR to GPIO35 (`3.3V–LDR–G35–10kΩ–GND`) and set
`PIN_LDR=35`. Don't use GPIO21 (that's the backlight).

## Calibrating the threshold

The ADC is 12-bit, so values run **0–4095**. On a CYD, bright light typically reads
**low** and darkness **high** (it may be inverted on your board).

1. Read your **bright** and **hand-covered (dark)** values from the serial monitor.
2. Put the midpoint into `DARK_THRESHOLD` in `main.cpp`.
3. If the relationship turns out to be inverted (high values in bright light), change
   the `val >= DARK_THRESHOLD` comparison in `loop()` to `val <= DARK_THRESHOLD`.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| The value never changes | Wrong pin; verify the CYD's LDR is GPIO34 |
| The value is always 0 or always 4095 | Cover/uncover the sensor; if it stays fixed, it's a pin or read problem |
| The logic is inverted ("dark" in bright light) | Flip the comparison, `>=` ↔ `<=` |
| The blue LED never lights | The threshold is out of range; set `DARK_THRESHOLD` from your real values |

## Next step

`04-touch` — touch (XPT2046) plus four-corner calibration. The heart of clawd's "tap
its head = allow" killer feature.
