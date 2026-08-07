# RAK3112 MeshCore MQTT Gateway

[![Build (PlatformIO)](https://github.com/nicolasumaras/rak3112-meshcore-mqtt-gateway/actions/workflows/build.yml/badge.svg)](https://github.com/nicolasumaras/rak3112-meshcore-mqtt-gateway/actions/workflows/build.yml)

Porting [`jmead/Meshcore-Repeater-MQTT-Gateway`](https://github.com/jmead/Meshcore-Repeater-MQTT-Gateway)
to the **RAK3112** (ESP32-S3 + SX1262).

**Goal:** one RAK3112 running standalone firmware that is simultaneously a MeshCore
repeater *and* a bidirectional MQTT bridge (mesh → MQTT publish, MQTT → mesh inject),
with no external Pi or bridge process.

---

## What it does now

The port outgrew its original scope. A single RAK3112 is a **full MeshCore
participant** with a web UI, not just an MQTT bridge:

| Capability | Status |
|---|---|
| SX1262 bring-up on the RAK3112 pin map | ✅ verified |
| MeshCore-compatible PHY (preamble derived from SF) | ✅ verified |
| Flood repeater with correct `path` handling | ✅ verified |
| Mesh → MQTT (real MeshCore frames as hex) | ✅ verified |
| MQTT → mesh | ✅ verified |
| On-device Ed25519 identity, persisted | ✅ verified |
| Public-channel messages (send + receive) | ✅ verified |
| Contacts learned from signature-verified adverts | ✅ verified |
| Direct messages via ECDH, with ACK | ✅ verified |
| `PATH` return so peers switch to direct routing | ✅ verified |
| Password-protected web UI | ✅ verified |
| Settings editable from the web UI (no USB needed) | ✅ verified |
| Outbound webhooks, up to 4 endpoints | ⚠️ partly — see below |
| NTP clock sync, independent of MQTT TLS | ✅ verified |
| Remote syslog + soak collector | ✅ verified |
| HTTP API for messages, contacts, config | ⚠️ untested |
| Remote firmware update (OTA) | ⚠️ untested |

Browse to the gateway's IP, log in as `admin`, and you get a contact list, a
message view, a send box for public or direct messages, "Announce me" to
broadcast a signed advert, and a Settings panel covering LoRa, node, WiFi, MQTT,
clock, logging, webhooks and firmware upload — no USB cable required.

**On the ⚠️ rows.** Webhook delivery is verified in production: real pager
traffic reaches a live endpoint and returns 200, with per-endpoint counters. What
has not been exercised is endpoints 2–4, since only one was ever configured. The
HTTP API and OTA both correctly refuse unauthenticated requests — OTA writes
nothing to flash without auth — but no authenticated call has been made from
this side, because that needs the operator's admin password.

### Things worth knowing before you deploy it

- **WiFi only runs when MQTT is enabled.** Inherited from upstream. Disabling
  MQTT silently takes down the web UI, syslog and webhooks, and recovery needs a
  cable. The UI now warns before letting you do it ([#18](../../issues/18)).
- **The web UI is plain HTTP.** Credentials and message bodies cross the LAN
  readable. Fine at home; put TLS in front of it otherwise.
- **OTA is remote code execution by design.** Anyone who can authenticate can
  replace the firmware. A wrong-but-valid image will flash and boot into
  something unreachable — recovery is USB.
- **DIRECT-routed packets are answered but not forwarded.** Relaying them needs
  path-shuffling against our identity hash, which is not implemented. Flood
  traffic repeats normally.

## Documentation

| Document | What it covers |
|---|---|
| **[docs/API.md](docs/API.md)** | HTTP API — messages, contacts, webhooks, config, NTP, firmware update. Working `curl` examples. |
| **[docs/FLASHING-RAK3112.md](docs/FLASHING-RAK3112.md)** | Build, flash, first boot, failure triage, and the proven/unproven split |
| **[docs/PORTING-PLAN.md](docs/PORTING-PLAN.md)** | The original port: findings, wire formats, phase breakdown |
| **[docs/UPSTREAM-README.md](docs/UPSTREAM-README.md)** | jmead's original README, preserved verbatim |

| Tool | What it does |
|---|---|
| **[tools/syslog_server.py](tools/syslog_server.py)** | Syslog collector. Events to a log file, heartbeats to CSV for plotting a soak. Standard library only. |
| **[tools/meshcore_send.py](tools/meshcore_send.py)** | Builds a valid MeshCore frame off-device and injects it via MQTT. Predates on-device messaging; kept as a reference implementation of the wire format. |

## Quick reference

```bash
# build and flash
pio run -e rak3112_mqtt -t upload

# serial config: 'c' for the menu, 'k' identity/contacts, 'm' send public message
screen /dev/cu.usbmodem* 115200        # exit with Ctrl-A K y

# API
GW=http://<gateway-ip>; AUTH='-u admin:YOUR_PASSWORD'
curl $AUTH "$GW/api/contacts"
curl $AUTH -X POST "$GW/api/messages" -H 'Content-Type: application/json' \
     -d '{"text":"hello","to":-1}'

# collect telemetry for a soak
python3 tools/syslog_server.py --port 5514

# update firmware over the network (no cable)
curl $AUTH -X POST "$GW/api/update" \
     -F "firmware=@.pio/build/rak3112_mqtt/firmware.bin"
```

Serial menu highlights: `3` LoRa · `4` repeater/hops · `10` admin password
(required for the web UI) · `13` clock/NTP · `16` remote logging. Radio, WiFi, MQTT and logging
changes all need a restart — they initialise once in `setup()`.

## Status

Running on real hardware against two LilyGo MeshCore pagers at 923.125 MHz /
SF8 / CR4/8 / BW62.5.

```
Initializing radio... success!        (MeshCore-compatible preamble: 32)
Restored 3 MeshCore contact(s)
✓ MeshCore web UI: http://<ip>
hb uptime=1020 heap=246036 minheap=235332 maxblock=237556 contacts=3
```

Verified in live use: packets received and repeated, public and direct messages
both directions, ACK and `PATH` return (pagers switch to direct routing after
the first exchange), contacts learned from signed adverts, mesh → MQTT and
MQTT → mesh, and webhook delivery to a live endpoint returning 200.

**What is still open** is in the ⚠️ rows above and the [issue tracker](../../issues):
endpoints 2–4 of the multi-webhook support, the authenticated HTTP API and OTA
paths, and the multi-day soak ([#13](../../issues/13)) — which needs elapsed
time rather than work.

### Bugs found upstream along the way

Bring-up turned up five defects in jmead's firmware, four of them
board-independent and affecting every user:

| Issue | Impact |
|---|---|
| [#17](../../issues/17) Phantom zero-byte RX | Publishes an empty payload to the broker on every transmit |
| Blind retransmission | Rebooted real pagers; would disrupt any mesh this firmware is near |
| [#18](../../issues/18) Menu ejects on a stray keystroke | Config appears dead while being fine |
| Unvalidated numeric ranges | Entering `256` silently becomes `0`, disabling the repeater |
| [#21](../../issues/21) `/raw` injection accepted then dropped | Silent, no diagnostic |

All fixed here with hardware evidence. [#14](../../issues/14) tracks offering
them upstream — the retransmission fix is the one worth sending first and alone.

### Repo layout

`jmead`'s firmware is vendored at `b9c1a33` in a **pristine** commit (`e0e3f79`), with the
RAK3112 port as an isolated commit on top. The upstream-ready patch is therefore exactly:

```bash
git diff e0e3f79..HEAD -- src platformio.ini .github
```

## The headline finding

The original draft plan assumed this was an SX1276 → SX1262 driver port, budgeted at
**3–6 days with 1–3 days in Phase 2**. Reading the actual upstream source shows that is
not the shape of the work:

> **jmead's firmware already supports the SX1262.** The `heltec_v3_mqtt` environment
> constructs an `SX1262`, is in the CI build matrix, and shipped in merged PR #3.

So the RAK3112 is not a new chip family for this codebase — it is a **new board on an
already-supported chip**. The realistic budget is **roughly 1–2 days**, most of it
bring-up and validation rather than driver work. The code delta is on the order of
**five small edits**:

| # | File | Change |
|---|------|--------|
| 1 | `platformio.ini` | New `[env:rak3112_mqtt]` |
| 2 | `src/config.h` | New `#elif defined(RAK_3112)` pin branch |
| 3 | `src/main.cpp:29-35` | Add `RAK_3112` to the existing SX1262 construction branch |
| 4 | `src/main.cpp:339-371` | Add an SX1262 `begin()` branch |
| 5 | `.github/workflows/*` | Add the env to both CI matrices |

Nothing in `mqtt_handler.h`, `settings_manager.h`, or `serial_config.h` needs to change.
The RAK3112 is an ESP32-S3, so WiFi, `Preferences`, `ESP.getEfuseMac()`, `ESP.restart()`,
and `IRAM_ATTR` all work as-is.

> The upstream README says RAK support "requires porting the WiFi/time/MQTT stack."
> That is true of the **RAK4631 (nRF52)** and does **not** apply to the RAK3112.

## Verified RAK3112 pin map

Confirmed line-by-line against `meshcore-dev/MeshCore` @ `03b6ef4`,
`variants/rak3112/platformio.ini`. All 19 values in the original draft were correct.

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| NSS (CS) | 7 | | SCLK | 5 |
| RESET | 8 | | MISO | 3 |
| BUSY | 48 | | MOSI | 6 |
| DIO1 | 47 | | TX LED | 46 |
| I2C SDA / SCL | 9 / 40 | | VEXT enable | 14 |
| GPS RX / TX | 43 / 44 | | User button | none (`-1`) |

```
USE_SX1262
SX126X_DIO2_AS_RF_SWITCH = true
SX126X_DIO3_TCXO_VOLTAGE = 1.8
SX126X_CURRENT_LIMIT     = 140
SX126X_RX_BOOSTED_GAIN   = 1
LORA_TX_POWER            = 22
board                    = esp32-s3-devkitc-1
```

Three corrections to assumptions in the draft plan:

- **No PSRAM.** Upstream sets no `BOARD_HAS_PSRAM` and no `board_build.*` overrides.
  The stock `esp32-s3-devkitc-1` JSON is 8 MB QIO / `default_8MB.csv` / **no PSRAM**.
  Do not add `-DBOARD_HAS_PSRAM` speculatively.
- **VEXT (GPIO14) is not required for the radio.** Upstream constructs it and leaves it
  **low**, never claims it, and the SX1262 works anyway. It is peripheral-only here, and
  currently vestigial — inherited from the Heltec V3 board file this variant was forked from.
- **`ARDUINO_USB_CDC_ON_BOOT=1` is mandatory**, not optional. Without it there is no serial
  console on ESP32-S3 native USB — and jmead's entire configuration UI is an interactive
  serial menu.

## Caveat on the pin map's provenance

The upstream RAK3112 variant has **no CI coverage** — it appears in none of MeshCore's
build workflows. These pins are community-contributed and not continuously validated.
They are still by far the best available starting point, but treat a bring-up failure as
"possibly the pin map" rather than assuming it is your code.

## Fallbacks

If the port stalls, two options work today:

- **Outbound-only, standalone:** [`lorddc1989/MeshCore-MQTT`](https://github.com/lorddc1989/MeshCore-MQTT)
- **Full bidirectional, external bridge:** companion firmware + [`ipnet-mesh/meshcore-mqtt`](https://github.com/ipnet-mesh/meshcore-mqtt) on a Pi

## Credits

- [`jmead/Meshcore-Repeater-MQTT-Gateway`](https://github.com/jmead/Meshcore-Repeater-MQTT-Gateway) — MIT
- [`meshcore-dev/MeshCore`](https://github.com/meshcore-dev/MeshCore) — RAK3112 variant / pin map
