# RAK3112 MeshCore MQTT Gateway

[![Build (PlatformIO)](https://github.com/nicolasumaras/rak3112-meshcore-mqtt-gateway/actions/workflows/build.yml/badge.svg)](https://github.com/nicolasumaras/rak3112-meshcore-mqtt-gateway/actions/workflows/build.yml)

Porting [`jmead/Meshcore-Repeater-MQTT-Gateway`](https://github.com/jmead/Meshcore-Repeater-MQTT-Gateway)
to the **RAK3112** (ESP32-S3 + SX1262).

**Goal:** one RAK3112 running standalone firmware that is simultaneously a MeshCore
repeater *and* a bidirectional MQTT bridge (mesh → MQTT publish, MQTT → mesh inject),
with no external Pi or bridge process.

---

## Status

**The radio works on real hardware.** Flashed to a RAK3112 (ESP32-S3 rev v0.2, 16 MB flash,
8 MB PSRAM) and the SX1262 came up first try on the ported pin map:

```
Initializing radio... success!
✓ Radio listening for packets
```

Transmit is confirmed too — the built-in `t` self-test sends successfully and the radio
returns to RX.

**Still unproven: everything involving a second device or a network.** No other MeshCore node
was in range, so over-the-air RX is untested, and WiFi/MQTT have not been configured — so
neither bridging direction has been exercised. [#10](../../issues/10), [#12](../../issues/12)
and [#13](../../issues/13) remain open.

Bring-up also turned up a genuine **upstream** bug affecting all boards, not just this port —
every transmit produced a phantom zero-byte receive, which with MQTT enabled would publish an
empty payload to the broker on every transmitted or forwarded packet. Found, fixed and
verified on hardware: [#17](../../issues/17).

See [docs/FLASHING-RAK3112.md](docs/FLASHING-RAK3112.md#what-is-and-isnt-proven) for the full
proven/unproven split.

- **[docs/FLASHING-RAK3112.md](docs/FLASHING-RAK3112.md)** — build, flash, and failure triage
- **[docs/PORTING-PLAN.md](docs/PORTING-PLAN.md)** — full plan and findings
- **[Issues](../../issues)** — per-phase progress

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
