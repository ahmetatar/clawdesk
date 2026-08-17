# clawd desk mascot — idea document

> **Concept:** bring **clawd**, the Claude Code mascot, to life physically on an
> ESP32-based TFT display (a CYD — "Cheap Yellow Display" / ESP32-2432S028R). While
> Claude Code works, the device reacts in real time — cute and funny.

---

## Hardware: the board we have (CYD / ESP32-2432S028R)

Beyond the basic specs, this board typically includes:

- **2.8" colour TFT (ILI9341, 240x320)** → clawd's face / animation canvas
- **Resistive touch (XPT2046)** → tap its head, pet it, allow or cancel
- **RGB LED** → mood, plus the "I'm thinking" breathing light
- **LDR light sensor** → sleep mode when the room is dark
- **Speaker output** → Tamagotchi-style chirps
- **microSD slot** → animation frames, sounds, clawd skins
- **WiFi + BLE** → wireless communication with the PC
- **USB-C** → power, programming and serial

---

## The brain: Claude Code hook events

Every idea here rests on these signals. Claude Code hooks print JSON to stdin on
each event; pipe that to the device and clawd comes alive.

| Hook event | When it fires | Data available |
|---|---|---|
| `SessionStart` | session starts/resumes | cwd, model |
| `UserPromptSubmit` | you send a prompt | the prompt text |
| `PreToolUse` | **before** a tool runs | tool name + input (and it can **block**) |
| `PostToolUse` | a tool finished | result / error |
| `Notification` | permission needed / gone idle | message |
| `Stop` | the answer is finished | — |
| `SubagentStop` | a subagent finished | — |
| `PreCompact` | before compacting a full context | — |
| `SessionEnd` | session ends | — |

An extra channel: the **statusline** script provides the model, context percentage
and cost on every render — good for a "live health indicator".

---

## A) Live status — clawd *lives* what I'm doing

- **A costume per tool.** clawd changes appearance based on `tool_name` in `PreToolUse`:
  - `Bash` → hacker mode, holding a terminal, sunglasses down 🕶️
  - `Edit`/`Write` → picks up a pen and writes
  - `Read`/`Grep`/`Glob` → searching with a magnifier 🔍
  - `WebFetch`/`WebSearch` → scanning the horizon with binoculars / surfing 🏄
  - `Task`/subagent → clawd **clones itself**, mini clawds scurrying about
    (a fan-out visualization!)
- **The "thinking" breathing light.** While I think, the RGB LED breathes slowly like
  Claude Code's own spinner, and goes out when the answer finishes (`Stop`).
- **A context/energy bar.** Take the context percentage from the statusline and show
  it as clawd's "coffee/energy" bar; as it fills, clawd gets tired.

## B) Funny event reactions — the soul of the project

- **Errors as comedy.** If `PostToolUse` reports a failure, clawd facepalms with an
  "ugh". Tests pass → confetti + green LED + "ta-da!". Tests explode → a small panic
  and a sad trombone 🎺.
- **Celebrating `git commit`.** Seeing `git commit` in a Bash input, clawd plants a
  flag; `git push` launches a rocket 🚀.
- **`PreCompact` = "brain defrag".** When the context is squeezed, clawd holds its
  head — "ugh, my memory is full" — then shakes it off and carries on.
- **Reacting to your prompt.** Looking at the text in `UserPromptSubmit`: "fix the
  bug" → cracks its knuckles; a very long prompt → eyes widen, "wow"; swearing or
  frustration → a sympathetic look 😅. Simple keyword/sentiment is enough.
- **Waiting on a long command.** If Bash takes a while (a build, say), clawd whistles
  and swings its feet, getting bored.

## C) Physical interaction — from the device **to Claude Code** (the killer feature)

The `PreToolUse` hook **runs synchronously and can block the tool**, which enables:

- **Allow/deny by touch.** For a tool that needs permission, clawd bangs on the
  screen, flashes the LED and plays a "hey!". **Tap its head → "yes, run it"**, swipe
  → "cancel". The hook script waits for the touchscreen answer and returns
  allow/deny. Physical permission management without looking at the monitor.
- **A "still waiting" reminder.** After `Stop`/`Notification`, clawd looks at you and
  taps its foot; if you stay away long enough (and the LDR reads dim), it falls
  asleep 😴.
- **Petting.** Touch an idle clawd and it purrs or emits a heart.

## D) Touches that exploit the hardware

- **LDR**: a "getting late" mode when the room darkens; clawd yawns.
- **Speaker**: a small Tamagotchi-style chirp per event.
- **SD card**: animation frames, sounds and alternative clawd skins.

## E) Advanced

- **Voice: "clawd, what are you doing?"** — add an I2S microphone, send raw audio to
  the PC, Claude summarises, and clawd moves its lips.
- **A multi-agent board.** With several agents running via Workflow/Task, show each
  as a mini clawd on screen.
- **A daily summary** (`SessionEnd`): tools run, files touched, commits made — clawd
  presents a small report.

## F) v2 "wow" ideas (brainstorm)

These build on the existing protocol (`clawd-device-protocol.md`); most **need no new
protocol events**, since the device can derive them from what it already receives.

### F1) The reverse channel — device to Claude Code, beyond permissions
Today only `/perm` goes device→PC. Widening that has the biggest impact:
- **Tap → inject "continue".** After `Stop`, while clawd looks at you, tapping its
  head makes the device hold a "queued prompt" (`continue`, or repeat the last
  prompt) that the next hook picks up — a desktop "forward" button.
- **Press and hold → interrupt.** During a long or misguided run, holding sets a
  "cancel flag"; the running `PreToolUse`/poll sees it and falls to `deny`. A
  physical ESC.
- **Swipe → quick answer.** For non-permission Notifications of the "yes/no/1/2"
  kind, a directional swipe is the choice.

### F2) Drama from real data — exploit the HUD's `wk` (quota) and `ctx`
- **Quota panic.** Past 90% of the weekly quota, clawd sweats and dims its lights —
  "boss, we're running out of tokens" mode; at 100% it takes a break. The data is
  already in `/status`.
- **Context = coffee/energy** (the bar from section A, as a character): as `ctx`
  fills, clawd tires; `compact` defrags it and it comes back fresh.
- **Commit weight.** On `git commit`, clawd either plants a flag easily or struggles
  to lift a huge commit 🏋️, based on the diff size (the hook puts the line count in `d`).

### F3) A persistent Tamagotchi — state in NVS
- **Consecutive coding days (a streak).** The agreed design (deferred past v1):
  - **What counts as a day:** at least one `edit` (Edit/Write/MultiEdit) **or** a
    `git commit/push` that day; merely opening the session does not count. The device
    already receives `tool.pre g=edit` and `git`, so the protocol is unchanged.
  - **State in NVS:** `streak`, `best`, `lastDay` — with `lastDay` as an **epoch day**
    (NTP epoch/86400 + tz), which keeps the `+1` arithmetic clean.
  - **Display (the anchor):** on the day's first `edit`/`commit` — the moment the
    streak actually increments — a ~2 s "🔥 DAY N" celebration, then the normal HUD.
    A second session the same day does not re-trigger it.
  - **Breaking:** after a gap the streak returns to 1, with a brief "streak broken,
    your best was N" moment.
  - **Notes:** don't touch the counter before NTP has synced; these are days *clawd
    witnessed* (it sees nothing while powered off). Milestones (7/30/100) are a later
    layer.
- **A history of daily reports.** Store the `session.end` summary and let the
  touchscreen **rewind the day**, playing that day's tool timeline as a sped-up
  animation (touch = scrubber).

### F4) "Come here" when you're away — an escalating call
On `Notification kind=idle` (Claude is waiting for your input), clawd first waves,
then flails if you don't answer, and finally makes an unmistakable sound and
animation. A physical nudge so you never miss a permission prompt — possibly the one
feature that makes the device *necessary*.

### F5) Sonification — "following the code by ear"
A short, distinguishable tone per tool group (`exec/edit/read/web/agent`) turns a
session into a rhythm: without looking, you hear "now it's building, now it's
editing". The success flow becomes musical too (errors already have the sad trombone
in section B).

### F6) Ambient mood — a session thermometer
The **screen background** colour temperature drifts slowly with the success/failure
ratio of `tool.post ok`, giving a peripheral sense of whether the session is going
well.
> ⚠️ On this board the pure-red LED (GPIO4) and the LDR are dead — do this through
> the **screen background** rather than the RGB LED, and the "room went dark → sleep"
> idea has to be built without the LDR.

### F7) A faithful mirror — stop looking at the terminal
The spinner *word pool* is synced, but there is no live counter. Add elapsed time,
tokens/cost and the "esc to interrupt" hint to `/status` and show them on the device
→ a glanceable external status bar that replaces looking at the terminal.

---

## Bridge architecture

```
Claude Code
   │  hooks → JSON on stdin
   ▼
a small local script (Python/Node)  ──serial or WebSocket──►  ESP32 (CYD)
   │  normalizes and forwards the event                       clawd: anim + sound + LED + touch
   ▲                                                           │
   └───────────  touch response (permission: allow/deny)  ◄────┘
```

- **v1:** the device is already on the same machine over USB-C → talk over the
  **serial port** (no WiFi setup, minimal latency).
- **v2:** move to WebSocket and go wireless.

> Everything in A and B comes straight from **real hook events**, no guessing. Only
> the sentiment/keyword reactions to the prompt are somewhat heuristic.

---

## Proposed roadmap

1. **The event protocol** — hook JSON → a compact message for the device (the
   backbone of the brain).
2. **The hook + bridge script** — push a few events (`PreToolUse`, `Stop`) to the
   device and verify them over serial.
3. **The device side** — a simple clawd with 2-3 states in LVGL, switching animation
   on each incoming message.
4. **The allow/deny touch flow** (the killer feature).
5. **Enrichment** with sound, LED, LDR and skins.

---

## Open questions

- clawd's artwork: is there an existing sprite/art set, or do we draw a simple clawd
  face with a few expressions from scratch?
- Is the device always on the same machine over USB (serial is enough), or wireless?
