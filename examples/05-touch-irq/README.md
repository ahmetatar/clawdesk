# 05 — Touch IRQ (touch-to-wake)

"Sleep when idle, wake on touch" using the XPT2046's **IRQ pin** — a small rehearsal
of clawd's real idle/wake flow, and the IRQ sibling of `04-touch`.

## Polling (04) vs IRQ (05) — what's the difference?

| | Polling (04) | IRQ (05) |
|---|---|---|
| Construction | `ts(T_CS)` | `ts(T_CS, T_IRQ)` |
| How it works | Reads pressure over SPI every `loop()` | A touch pulls IRQ low; reads on the falling edge |
| Strength | **Continuous finger tracking** (drawing, dragging) | **Single tap / wake-up**, with no idle SPI traffic |
| In clawd | Permission flow, head taps, swipes | Waking from sleep |

> In IRQ mode `touched()` mainly catches the **falling edge** (the start of a touch);
> it does not re-trigger every loop while you hold. That makes IRQ ideal for
> tap/wake, and polling more comfortable for continuous tracking.

## Run

```bash
cd examples/05-touch-irq
pio run -t upload
```

## Success criteria

1. A green **"AWAKE"** on screen.
2. **5 seconds without touching** → a navy **"Zzz..."** (asleep).
3. **Touch the screen** → an immediate **"AWAKE!"** plus a blue LED flash, and
   `TOUCH -> AWAKE!` in the serial monitor.

If that works, the IRQ interrupt fires correctly on this board.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| It never sleeps | `SLEEP_MS` is too large, or noise is being read as a constant touch |
| Touching does not wake it | The IRQ interrupt isn't firing → IRQ is problematic on this board; **fall back to polling (04)** |
| No touch at all | Verify the separate bus and pins: HSPI + 25/33/32/39, IRQ=36 |

## Conclusion

If IRQ works, clawd can wake from sleep on a touch. If it doesn't, no problem — the
**polling approach in 04** is enough, since the device polls touch in its loop anyway.
Polling may well be the better choice in production.
