# 07 — Async events + permissions (the body of the protocol)

The real body of the clawd protocol on ESPAsyncWebServer: events plus the **touch
permission flow** (the killer feature). This moves from the built-in WebServer of
`06` to a non-blocking async one.

## Endpoints

| Endpoint | What it does |
|---|---|
| `POST /e` | Fire-and-forget event. Body = the `{k, d}` envelope. Returns **204**; the screen and LED react. |
| `POST /perm` | Permission question. Body `{id, d:{tool,s,risk}}`. Returns `{"pending":true}` and opens a prompt on screen. |
| `GET /perm/{id}` | Poll for the decision: `{"decision":"allow"\|"deny"}` or `{"pending":true}`. |
| `GET /health` | Liveness: `{"fw":"0.2.0","caps":["led","touch"]}`. |

## The critical constraint: callbacks must not touch the display

AsyncWebServer callbacks run **on a separate task**, and touching the display (SPI)
from one causes a crash. So:
- The `POST /e` callback pushes onto a **thread-safe FreeRTOS queue**; all drawing
  happens in `loop()`.
- The `POST /perm` callback writes into a **mutex-protected slot**; `loop()` draws the
  prompt and the touchscreen resolves it.

## Two important traps (solved in this example)

1. **The JSON handler swallows the GET.** By default
   `AsyncCallbackJsonWebHandler("/perm")` also catches `/perm/7` on every method, so
   `GET /perm/{id}` hits the POST handler and corrupts the state. Fix:
   `handler->setMethod(HTTP_POST)`.
2. **Regex routes.** `GET /perm/{id}` needs `-D ASYNCWEBSERVER_REGEX=1` plus
   `^\/perm\/([0-9]+)$`.

## Setup and run

Fill in `include/secrets.h` (see `06`), then:

```bash
cd examples/07-async-events
pio run -t upload
```

## Testing (from the PC)

```bash
IP=<the-ip-on-screen>   # or clawd.local

# events — the screen and LED change
curl -s -XPOST http://$IP/e -H 'content-type:application/json' -d '{"k":"tool.pre","d":{"g":"exec","s":"npm test"}}'
curl -s -XPOST http://$IP/e -H 'content-type:application/json' -d '{"k":"git","d":{"op":"commit"}}'

# permission flow — a PERMIT? prompt opens; top half = allow, bottom half = deny
curl -s -XPOST http://$IP/perm -H 'content-type:application/json' -d '{"id":7,"d":{"tool":"Bash","s":"git push","risk":"med"}}'
curl -s http://$IP/perm/7     # after touching -> {"decision":"allow"} | {"deny"}
```

## Success criteria (verified)

- `/e` events return 204 and the screen cycles through HELLO/HACKING/SEARCHING/OOPS/
  GIT!/DEFRAG…
- `/perm` shows **PERMIT?** plus the tool and risk; the **top half is ALLOW (green)**
  and the **bottom half DENY (red)**.
- After the touch, `GET /perm/{id}` returns the decision **persistently** and
  **idempotently**.

## Next step

The PC-side **hook script**: it normalizes Claude Code hook events (`PreToolUse`,
`Stop`, …) and POSTs them to `clawd.local`, blocking on `POST /perm` plus a poll for
tools that need permission. After that comes the real Claude Code integration.
