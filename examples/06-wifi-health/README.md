# 06 — WiFi + mDNS + /health

Joins WiFi, advertises itself as `clawd.local` and answers `GET /health` with JSON.
This is the **first step of the clawd protocol's transport layer** — the PC ↔ device
bridge coming up.

It uses the built-in `WebServer` from the ESP32 core, with no external library. When
we add the `/e` and `/perm` endpoints (the next step) we switch to ESPAsyncWebServer.

## Setup: WiFi credentials

Fill in `include/secrets.h` with your own network (the file is gitignored, so the
password never enters the repo):

```c
#define WIFI_SSID "your_wifi_name"
#define WIFI_PASS "your_wifi_password"
```

> ⚠️ The ESP32 only connects to **2.4 GHz** WiFi (no 5 GHz support). Use your
> network's 2.4 GHz band.

## Run

```bash
cd examples/06-wifi-health
pio run -t upload
```

## Success criteria

- A green **"clawd ONLINE"** on screen with the device's **IP address** and
  `http://clawd.local/health`.
- `WiFi OK. IP: ...` and `HTTP server :80 up` in the serial monitor.
- From the PC (on the same network):
  ```bash
  curl http://clawd.local/health
  # -> {"fw":"0.1.0","caps":["led","touch"]}
  ```
  If `clawd.local` does not resolve, use the IP on screen: `curl http://<IP>/health`.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| "WiFi CONNECTION FAILED" on screen | Wrong SSID/password, or a 5 GHz network (use 2.4 GHz) |
| `clawd.local` does not resolve | mDNS stalls on some networks/PCs; curl the IP shown on screen |
| curl cannot connect although the IP is shown | Are the PC and the device on the **same network/VLAN**? Guest-network isolation? |
| It keeps connecting and never gets an IP | The signal may be weak; move it closer to the router |

## Next step

`07` — moving to ESPAsyncWebServer plus `POST /e` (fire-and-forget events) and
`POST /perm` (the touch permission flow), the real body of the clawd protocol. After
that, a hook script pushes real Claude Code events to `clawd.local`.
