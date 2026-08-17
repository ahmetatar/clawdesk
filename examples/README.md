# clawd examples

Small, standalone PlatformIO projects for getting to know the CYD /
ESP32-2432S028R board one piece at a time. Each folder teaches **one thing** and
builds and uploads on its own. The order follows the practical steps in
`../clawd-cyd-guide.md`.

| Project | What it teaches | Guide step |
|---|---|---|
| `01-hello-world` | Bring up the display and draw text (the hardest trap) | 3.2 |
| `02-blink-rgb` | The on-board RGB LED, active-low | 4.1 |
| `03-ldr` | Reading the light sensor (missing/faulty on this board) | 4.2 |
| `04-touch` | Touch (XPT2046, separate SPI/HSPI, polling) + calibration | 4.3 |
| `05-touch-irq` | Touch-to-wake using the touch IRQ (sleep/wake) | 4.3+ |
| `06-wifi-health` | WiFi + mDNS (clawd.local) + GET /health | 6 |
| `07-async-events` | ESPAsyncWebServer: POST /e + touch permissions (/perm) | 7 |

Run each project from its own folder:

```bash
cd examples/01-hello-world
pio run -t upload && pio device monitor
```
