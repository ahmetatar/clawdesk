# TASK: consumer onboarding — pre-flashed device + runtime WiFi provisioning

**Status:** ON HOLD (revisit later)
**Created:** 2026-07-19
**Goal:** make the "buy → install the plugin → run one command → clawd is ready" flow
genuinely possible for a device sold to a customer.

## Target flow (customer)

1. Take the device out of the box (firmware **pre-flashed**, no source or toolchain
   needed).
2. Connect a phone to the device's SoftAP, pick their WiFi and enter the password.
3. Run **one command** on the PC (global plugin install + statusLine + `CLAWD_HOST`).
4. clawd is ready.

## Why that isn't possible today (analysis, 2026-07-19)

The current flow is "build from source + flash over USB" — a developer flow. The
blockers:

1. **WiFi is baked in at build time (the real blocker).** `include/secrets.h` defines
   `WIFI_SSID`/`WIFI_PASS` and `connectWiFi()` uses those macros, so changing WiFi
   means rebuilding and reflashing. There is no runtime provisioning.
2. **The customer needs the toolchain and the source.** `install.sh` builds the
   firmware from source (`pio run -t upload`), requiring platformio, jq, curl,
   claude-code and the repo.
3. **The static IP is also a build-time constant** (`include/config.h`) pinned to one
   specific network. The shipped default would be wrong for the recipient's network.
4. **The PC finds the device's IP only during flashing.** `install.sh` captures the IP
   from the serial port while flashing, and mDNS (`clawd.local`) is unreliable behind
   an extender.

## What already works today

The PC side is close to a single command and is solid: global plugin install, the
statusLine bridge, spinner sync and the `CLAWD_HOST` env. The **firmware half**
(provisioning) is what's missing.

## To do

### A) Firmware — self-provisioning (the linchpin)
- [ ] On first boot, if NVS (`Preferences`) holds no WiFi credentials, start a
      **SoftAP + captive portal** (a `clawd-setup` network).
- [ ] The portal: the customer picks their WiFi and enters the password → the
      credentials are written to NVS → reboot → it connects.
- [ ] Change `connectWiFi()` to "read from NVS, otherwise open the portal", removing
      the dependency on `include/secrets.h`.
- [ ] Default to **DHCP** (drop the build-time static IP). If a static address is
      needed, derive it at runtime on the first connection.
- [ ] After connecting, show the device's IP **on screen** (and in the portal) so the
      customer can enter it into the PC command — this is how to get around mDNS's
      unreliability.
- [ ] A "reset WiFi" path (a long press or a special gesture) → clear NVS, reopen the
      portal.

### B) PC — one command
- [ ] Split `install.sh` in two: the customer has no firmware build/flash step. The
      remaining command is just the global plugin install + statusLine wiring +
      setting `CLAWD_HOST` (all of which already exist).
- [ ] `CLAWD_HOST` input: the customer supplies the IP shown by the portal or the
      screen (or tries mDNS).

### C) Production / distribution
- [ ] A factory flashing procedure: one `firmware.bin` (no WiFi, with provisioning)
      for every device.
- [ ] README / in-box quick guide: "give it WiFi from your phone + run one command on
      the PC".

## Notes
- Related memory: `clawd-host-use-ip` (mDNS times out behind an extender),
  `clawd-install-uninstall-scripts`, `device-memory-budget` (huge_app partition — a
  captive portal's HTTP + DNS needs extra flash, check the headroom).
- A SoftAP captive portal on the ESP32 needs `DNSServer` plus a
  `WebServer`/`AsyncWebServer`; `AsyncWebServer` is already present.
