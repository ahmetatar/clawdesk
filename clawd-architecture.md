# clawd — hook + ESP32 architecture

> The architectural plan for the hook (PC bridge) and the ESP32 (PlatformIO)
> firmware. **No code yet** — layers, folder structure, library choices and open
> questions.

## Overall layout

Two parts, one repo:

```
clawdesk/
├── hooks/                  # PC side — the bridge into Claude Code
│   ├── dispatch            # single entry point (stdin JSON → HTTP POST)
│   ├── lib/                # normalization: tool group, git, mood, risk
│   └── clawd.config        # host (clawd.local), token, timeouts
│
├── firmware/               # ESP32 PlatformIO project
│   ├── platformio.ini
│   ├── src/
│   ├── include/
│   ├── lib/
│   └── data/               # animation frames, sounds (LittleFS/SD)
│
├── clawd-device-protocol.md
└── clawd-architecture.md
```

---

## A) Hook-side architecture (PC)

**Attachment point:** the `hooks` block in `~/.claude/settings.json`. Every event
(`PreToolUse`, `PostToolUse`, `Stop`, `UserPromptSubmit`, `Notification`,
`SessionStart/End`, `PreCompact`) calls the same script, which decides what to do
based on `hook_event_name`.

**One dispatcher**, rather than a script per event:

```
stdin (JSON from Claude Code)
   │
   ▼
dispatch
   ├─ parse: hook_event_name, tool_name, tool_input, ...
   ├─ normalize: tool→group, detect "git commit/push", mood, risk
   ├─ map: hook event → protocol kind (k)
   └─ emit:
        ├─ normal event      → POST /e  (fire-and-forget, background, ~0.3s timeout)
        └─ PreToolUse permit → POST /perm + GET /perm/{id} poll (blocking)
```

Separation of concerns: **the dispatcher knows and normalizes "what happened".** The
device stays dumb.

**Decisions (hook side):**
- **Language:** `bash + curl + jq` (plain, no dependencies) vs `python` (more
  readable once the logic grows).
- **The permission hook behaves differently:** the `PreToolUse` matcher blocks only
  for tools that need permission; everything else is fire-and-forget.
- **Fault tolerance:** if the device is off, `dispatch` must exit 0 silently — a hook
  error must never affect Claude Code.

---

## B) ESP32 firmware architecture (PlatformIO)

### Layers

```
┌─────────────────────────────────────────────┐
│  net/        WiFi + mDNS + AsyncWebServer   │  ← /e, /perm, /health routes
├─────────────────────────────────────────────┤
│  protocol/   JSON envelope → Event struct   │  ← ArduinoJson parsing
├─────────────────────────────────────────────┤
│  core/       EventQueue + StateMachine      │  ← clawd state, mood engine, perm store
├─────────────────────────────────────────────┤
│  render/     display + animation player     │  ← frame player
├─────────────────────────────────────────────┤
│  io/         touch · RGB LED · buzzer · LDR │
└─────────────────────────────────────────────┘
```

### The critical decision: the concurrency model

`ESPAsyncWebServer` callbacks run in the background, in a separate context.
**Touching the display from a callback is forbidden** (SPI collision, crash). The
correct flow:

```
HTTP callback (async)                 render loop (loop() or its own task)
  parse → Event                          drain EventQueue
  push onto EventQueue  ─────────────►   StateMachine.apply(event)
  /perm: record in PermStore,            draw the animation, drive LED/sound
         hand out a ticket/id            read touch → PermStore.resolve(id)
```

**A thread-safe queue between net and render** (a FreeRTOS queue). The web server
only writes to the queue; all drawing happens in one place. Permissions work the
same way: the callback records a "pending" entry in `PermStore`, `GET /perm/{id}`
reads it, and the touchscreen resolves it.

**Dual-core:** AsyncTCP runs in its own task; rendering happens in `loop()` or in a
separate task pinned to Core 1. `loop()` is enough to start.

### Stack / library choices

| Topic | Option A | Option B | Note |
|---|---|---|---|
| **Drawing** | LVGL (widgets/animation built in) | raw TFT_eSPI + sprite blit | LVGL is polished but heavy; raw TFT_eSPI may be leaner and faster for frame-based characters |
| **Animation storage** | SD card (plenty of room) | LittleFS (internal flash) | On the CYD the SD shares SPI with the TFT — careful; LittleFS is trouble-free but ~1.5MB |
| **Web server** | ESPAsyncWebServer + AsyncTCP | synchronous WebServer | Async recommended (non-blocking, easier to hold permissions) |
| **JSON** | ArduinoJson | — | standard |
| **WiFi setup** | WiFiManager | hardcoded | captive portal recommended |
| **mDNS** | ESPmDNS | — | `clawd.local` |

**Recommended starting stack:** TFT_eSPI + sprite-based frame animation + LittleFS —
the fewest surprises and the fastest first light. LVGL can come later, once the menu
and permission screens get richer.

### CYD-specific pitfalls to plan for

- **Board:** `ESP32-2432S028R`. In `platformio.ini`, the `esp32dev` env plus the
  TFT_eSPI pins via **build_flags** (ILI9341, custom pins) — the most common snag.
- **Touch:** XPT2046, on most CYDs on a **separate SPI bus** from the TFT, so it
  needs its own SPI instance (a well-known gotcha).
- **Hardware pins (to verify):** RGB LED ~GPIO 4/16/17 (active-low), LDR ~GPIO34,
  buzzer ~GPIO26 (DAC), SD on a separate SPI.
- **SD vs TFT sharing SPI:** using both at once requires bus management, so putting
  the animations on LittleFS is simpler at first.

---

## Open decisions (before coding)

1. **Hook language:** bash+curl or python?
2. **Drawing layer:** raw TFT_eSPI sprites or LVGL?
3. **Animation storage:** LittleFS or SD?
4. **Where to render:** `loop()` or a pinned task? (start with `loop()`)
5. **Animation source:** is there an existing clawd sprite set, or do we produce the
   expressions from scratch?

**Recommended defaults:** bash · TFT_eSPI · LittleFS · loop().
