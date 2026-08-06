# Porting Plan: jmead/Meshcore-Repeater-MQTT-Gateway → RAK3112

**Target:** RAK3112 (ESP32-S3 + SX1262) running standalone MeshCore repeater firmware with
bidirectional MQTT bridging.

**Revision:** this document supersedes the original draft plan. Both of the draft's stated
unknowns — "I haven't been able to browse the raw source tree" and "verify the pin map" —
have been resolved, and the findings materially reshaped the plan. Changes are called out
in [Appendix A](#appendix-a--what-changed-from-the-draft-plan).

---

## 1. What the upstream code actually looks like

`jmead/Meshcore-Repeater-MQTT-Gateway` @ `b9c1a33` (main, last push 2025-11-20). MIT.
~3,000 lines across six files, all header-only except `main.cpp`.

| File | Lines | Role |
|---|---|---|
| `src/main.cpp` | 906 | **All radio logic** + mesh/repeater + stats + serial dispatch + `setup()`/`loop()` |
| `src/mqtt_handler.h` | 670 | WiFi/Ethernet, TLS, PubSubClient, topics, NTP |
| `src/serial_config.h` | 746 | Interactive serial config UI |
| `src/settings_manager.h` | 254 | NVS `Preferences` persistence |
| `src/config.h` | 335 | Pin `#ifdef` chain, config structs, defaults |
| `src/ca_cert.h` | 30 | Embedded CA PEM |

There is no `radio.h`, no HAL, and no board-support directory.

### 1.1 SX1262 support already exists

`src/main.cpp:28-35` — the only radio construction site in the repo:

```cpp
#ifdef RAK4631_ETH
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
#elif defined(HELTEC_V3)
SX1262 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_BUSY);
#else
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
#endif
```

Two SX1262 paths exist already:

- **`HELTEC_V3`** — ESP32-S3, WiFi, SX1262, **in the CI matrix**, merged in PR #3.
  This is the correct template for RAK3112.
- **`RAK4631_ETH`** — nRF52 + Ethernet, **not in either CI matrix**, and almost certainly
  does not compile today (`serial_config.h` includes `<WiFi.h>` unconditionally,
  `settings_manager.h` includes `<Preferences.h>` unconditionally, `main.cpp:189` calls
  `ESP.getEfuseMac()`). **Do not use this as the template** despite the name similarity.

> Naming wart to be aware of: the `HELTEC_V3` branch passes `LORA_DIO0` as the SX1262 IRQ
> argument. On Heltec V3 that GPIO is physically DIO1 — the macro name is misleading but the
> value is right. For RAK3112, name the macro `LORA_DIO1` and pass it in the IRQ slot.

### 1.2 Radio/MQTT coupling

Mostly decoupled, in the direction that matters:

- **MQTT → radio** is cleanly decoupled by callback. `MQTTHandler` holds a
  `MQTTMessageCallback`; `main.cpp:238-242` registers a lambda calling `sendLoRaPacket`.
  `mqtt_handler.h` contains **zero** references to `radio`, RadioLib, or any `SX12xx` type.
- **Radio → MQTT** is a direct call — `handleLoRaPacket()` (`main.cpp:510`) calls
  `mqttHandler->publishRawPacket(...)` on a global pointer. One-directional and confined
  to `main.cpp`, so it does not obstruct the port.

The real friction is that chip selection is **duplicated across two `#ifdef` chains
300 lines apart** (`main.cpp:29-35` construction, `main.cpp:339-371` `begin()`). Any new
board must edit both.

### 1.3 Chip-specific assumptions beyond init — the good news

The complete set of `radio.*` calls in the repo, all in `main.cpp`:

| Line | Call | Portability |
|---|---|---|
| 341/352/361 | `begin(...)` | Already branched per chip |
| 350 | `setDio2AsRfSwitch(true)` | RAK4631 branch only — SX126x-specific |
| 380 | `setCRC(true)` | Both families |
| 388 | `setOutputPower(txPower, true)` | SX127x-only 2-arg form, but already fenced to LilyGo |
| 401 | `setPacketReceivedAction(setRadioFlag)` | **Family-agnostic** |
| 405, 500, 714, 724, 779 | `startReceive()` | Family-agnostic |
| 475 | `readData(buffer, sizeof(buffer))` | Family-agnostic |
| 480-482 | `getPacketLength()` / `getRSSI()` / `getSNR()` | Family-agnostic |
| 706 | `transmit(...)` | Family-agnostic |

**The draft's biggest feared risk does not exist.** There is no manual `attachInterrupt`,
no `setDio0Action`/`setDio1Action` anywhere — the code uses RadioLib's unified
`setPacketReceivedAction`. Likewise:

- **Packet sizes are fine.** RX buffer is a fixed `uint8_t[256]`; TX rejects `>255`.
  SX1262's FIFO is also 255.
- **No SX1276-tuned timing constants.** `handleLoRaReceive()` is polled every `loop()`.
  The only radio-adjacent delay is `delay(random(100, 300))` before repeat retransmit.

Three things are *absent* that the RAK3112 port should consider adding:
`setCurrentLimit()`, `setRxBoostedGainMode()`, and any TX-power clamp.

### 1.4 Other constraints

- **RadioLib is pinned `^6.6.0`** → resolves to exactly 6.6.0 (last 6.x).
  `setPacketReceivedAction` exists there; no API migration is forced.
- **`platform = espressif32` is unpinned** in every env — resolves to latest. For a
  reliable ESP32-S3 build, pin it (upstream MeshCore pins `platformio/espressif32@6.11.0`).
- **TX power is never clamped.** `config.lora.txPower` (default 20) goes straight into
  `begin()`. The serial menu prompts "2-20 dBm" but does not enforce it. SX1262 max is 22.
- **`setupLoRa()` runs once**, from `setup()` (`main.cpp:229`). Changing LoRa params in the
  serial menu does **not** re-init the radio — a reboot is required. Expect this during
  bring-up; it is not a bug you introduced.
- Default sync word is `0x12`, already commented as `RADIOLIB_SX126X_SYNC_WORD_PRIVATE`.

---

## 2. Verified RAK3112 target definition

Confirmed against `meshcore-dev/MeshCore` @ `03b6ef4`, `variants/rak3112/platformio.ini`.
**All 19 claimed values in the draft were correct — zero refutations.**

```ini
board = esp32-s3-devkitc-1

-D P_LORA_NSS=7        -D P_LORA_SCLK=5     -D P_LORA_TX_LED=46
-D P_LORA_RESET=8      -D P_LORA_MISO=3     -D PIN_BOARD_SDA=9
-D P_LORA_BUSY=48      -D P_LORA_MOSI=6     -D PIN_BOARD_SCL=40
-D P_LORA_DIO_1=47                          -D PIN_VEXT_EN=14
-D PIN_GPS_RX=43       -D PIN_GPS_TX=44     -D PIN_USER_BTN=-1

-D USE_SX1262
-D SX126X_DIO2_AS_RF_SWITCH=true
-D SX126X_DIO3_TCXO_VOLTAGE=1.8
-D SX126X_CURRENT_LIMIT=140
-D SX126X_RX_BOOSTED_GAIN=1
-D LORA_TX_POWER=22
-D ESP32_CPU_FREQ=80
-D ARDUINO_USB_CDC_ON_BOOT=1
```

### 2.1 Corrections to draft assumptions

**No PSRAM.** Upstream sets no `BOARD_HAS_PSRAM`, no `-mfix-esp32-psram-cache-issue`, no
`board_build.arduino.memory_type`, and no `board_build.*` overrides at all. The stock
`esp32-s3-devkitc-1` JSON (platform 6.11.0) is *"ESP32-S3-DevKitC-1-N8 (8 MB QD, No PSRAM)"*,
`flash_mode: qio`, `partitions: default_8MB.csv`. The draft's "plus `BOARD_HAS_PSRAM` if the
module has PSRAM" should be dropped unless your specific module is verified to have it.

**VEXT / GPIO14 is not a radio power gate.** `RAK3112Board.h` constructs
`periph_power(PIN_VEXT_EN)`; `RefCountedDigitalPin::begin()` is active-HIGH and drives the
pin **LOW** at boot. `periph_power.claim()` is **never called for rak3112** — and the SX1262
works anyway. `radio_init()` just calls `radio.std_init(&spi)` with no power gating.
*Caveat: this is verified software behavior, not a schematic reading.* If bring-up fails,
driving GPIO14 high is a cheap thing to try.

**`ARDUINO_USB_CDC_ON_BOOT=1` is load-bearing.** Combined with the board JSON's
`ARDUINO_USB_MODE=1`, this is native-USB CDC. Omit it and you get **no serial console** —
which means no access to jmead's interactive config menu, i.e. no way to set WiFi or MQTT
credentials. This is the single easiest way to brick your own bring-up.

### 2.2 Provenance caveat

`grep -rn "RAK_3112\|rak3112" .github/` in MeshCore returns **nothing**. None of the 11
`[env:RAK_3112_*]` targets appear in any build workflow. The variant is community-contributed
and not continuously validated. Corroborating its Heltec-V3-fork lineage: `RAK3112Board.h` is
a near-verbatim copy of `HeltecV3Board.h`, and `[env:RAK_3112_sensor]` still carries
`ADVERT_NAME='"RAK3112 v3 Sensor"'`.

Two upstream oddities **not** to copy:
- `[env:RAK_3112_repeater_bridge_rs232]` sets `WITH_RS232_BRIDGE_RX=5` / `TX=6` — the same
  GPIOs as `P_LORA_SCLK` and `P_LORA_MOSI`. That looks like a genuine conflict.
- `PIN_VBAT_READ=1` / `PIN_ADC_CTRL=36` are `#ifndef` defaults in `RAK3112Board.h`, inherited
  from Heltec V3 with only `PIN_ADC_CTRL` changed (37→36). Least likely to have been
  verified against real RAK hardware.

### 2.3 GPS

RAK3112 has **no onboard GPS** in this port. GPS is compiled in only because `[rak3112]`
inherits `${sensor_base.build_flags}` (which sets `ENV_INCLUDE_GPS=1`). It uses the generic
detect path, not the RAK one — the env defines `RAK_3112`, not `RAK_BOARD`. Detection is
opportunistic (`gps_detected = (Serial1.available() > 0)`). GPIO43/44 are the ESP32-S3 native
U0TXD/U0RXD, exposed for an optional external GPS. **Irrelevant to this port** — jmead's
firmware has no GPS feature. Ignore these pins.

---

## 3. Phases

Revised effort: **~1–2 days**, versus the draft's 3–6.

### Phase 0 — Baseline (2–3 hours)

1. Clone jmead's repo; build **`heltec_v3_mqtt`** specifically. It is the direct template for
   this port — an ESP32-S3 + SX1262 + WiFi target that already works. Building it proves the
   toolchain and gives a known-good reference binary.
2. Flash to a spare board if available; otherwise a clean compile is sufficient.
3. Note the resolved `espressif32` platform version (it is unpinned upstream).

*The draft's step 2 — "skim the source for how radio init is done" — is already done and
written up in §1. No need to repeat it.*

### Phase 1 — Build environment (1 hour)

Add `[env:rak3112_mqtt]` to `platformio.ini`, modeled on `heltec_v3_mqtt`:

```ini
[env:rak3112_mqtt]
platform = platformio/espressif32@6.11.0   ; pin — upstream leaves this floating
board = esp32-s3-devkitc-1
framework = arduino

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DRAK_3112
    -DMeshCore_MQTT_GATEWAY
    -DARDUINO_USB_CDC_ON_BOOT=1            ; mandatory — see §2.1
    ; NOTE: deliberately no -DBOARD_HAS_PSRAM — see §2.1

monitor_speed = 115200
monitor_filters = esp32_exception_decoder

lib_deps =
    jgromes/RadioLib@^6.6.0
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.3

upload_speed = 921600
```

Confirm it compiles **before** touching pins — this isolates "does ESP32-S3 build at all"
from "are the pins right."

### Phase 2 — Board support (1–2 hours, not 1–3 days)

Three edits.

**a. `src/config.h`** — add to the `#ifdef` chain at lines 34-103:

```cpp
#elif defined(RAK_3112)
  #define LORA_SCK    5
  #define LORA_MISO   3
  #define LORA_MOSI   6
  #define LORA_CS     7
  #define LORA_RST    8
  #define LORA_DIO1   47
  #define LORA_BUSY   48
  #define LORA_TX_LED 46
```

**b. `src/main.cpp:28-35`** — add to the SX1262 construction branch. RadioLib's `Module`
signature is `(cs, irq, rst, gpio)`, so for SX126x that is `(NSS, DIO1, RESET, BUSY)`:

```cpp
#elif defined(RAK_3112)
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);
```

**c. `src/main.cpp:339-371`** — add a `begin()` branch. Start from the `HELTEC_V3` form,
which passes TCXO voltage as a `begin()` parameter, then add the calls the RAK3112 needs
that no existing branch makes:

```cpp
#elif defined(RAK_3112)
  state = radio.begin(freq, bw, sf, cr, syncWord, txPower,
                      8,        // preamble
                      1.8F,     // DIO3 TCXO voltage
                      false);   // useRegulatorLDO (DC-DC)
  radio.setDio2AsRfSwitch(true);        // absent from the HELTEC_V3 branch
  radio.setCurrentLimit(140.0);         // absent from the whole repo
  radio.setRxBoostedGainMode(true);     // absent from the whole repo
```

Note this differs from the draft in one respect: **TCXO is a `begin()` parameter here**, not
a separate `setTCXO(1.8)` call, because that is how this codebase's existing SX1262 path does
it. Either works in RadioLib; matching the existing style keeps the diff reviewable.

Also cap TX power at 22 for this board — the repo has no clamp anywhere and the default is 20.

### Phase 3 — LoRa bring-up, MQTT disabled (2–4 hours)

Keep the draft's isolation discipline; it is sound.

1. Log received packets to serial instead of publishing. Confirm TX and RX against another
   MeshCore node.
2. **If `begin()` returns `-2` (`RADIOLIB_ERR_CHIP_NOT_FOUND`)**, the TCXO voltage is the
   prime suspect — this is the exact symptom reported in jmead's issue #2. Second suspect:
   the pin map (see §2.2 — it is uncovered by upstream CI). Third: try driving GPIO14 high.
3. Remember `setupLoRa()` only runs at boot — reboot after changing LoRa settings (§1.4).

### Phase 4 — Re-enable MQTT and validate end-to-end (2–4 hours)

1. **Mesh → MQTT:** a message from another MeshCore node appears on the broker.
2. **MQTT → mesh:** publishing to the downlink topic produces a real LoRa packet received
   by another device.
3. Confirm the serial config menu is reachable over native USB CDC (validates §2.1).

### Phase 5 — Soak (a few days, passive)

Run it as your actual repeater. Watch specifically for WiFi reconnect handling, MQTT
reconnect handling, and heap trend over time. ESP32-S3 without PSRAM has less headroom than
you might assume — the repo's dedup and neighbor-tracking structures live in `main.cpp` and
grow with mesh size.

### Phase 6 — Upstream (optional)

Open a PR against jmead's repo. It is a genuinely small, well-scoped addition and the README
already flags RAK as wanted. **Worth including in the PR description:** that RAK3112 is
ESP32-S3, so the README's "requires porting the WiFi/time/MQTT stack" caveat — written for
the nRF52 RAK4631 — does not apply. Also add the env to both CI matrices
(`.github/workflows/build.yml:14` and `release-assets.yml:25`).

---

## 4. Risks, re-ranked

| Risk | Draft's view | Actual |
|---|---|---|
| **TCXO voltage wrong → `begin()` returns -2** | not mentioned | **Highest.** Known symptom in jmead's issue #2 |
| **Pin map unvalidated upstream** | "risk is low, official variant uses it" | **Moderate.** No CI covers it; community-contributed |
| **Missing `ARDUINO_USB_CDC_ON_BOOT` → no console** | not mentioned | **Moderate**, and trivially avoidable |
| Unpinned `espressif32` platform drift | not mentioned | Low–moderate; pin it |
| DIO interrupt handling differences | called out as a real risk | **Eliminated.** Unified `setPacketReceivedAction` |
| Packet size limits | called out as a real risk | **Eliminated.** 256 buffer, 255 cap, matches SX1262 |
| SX1276-tuned timing constants | called out as a real risk | **Eliminated.** None exist |
| ESP32-S3 GPIO capability conflicts | called out, judged low | **Confirmed low** |

---

## Appendix A — What changed from the draft plan

The draft was directionally right and its pin map was **100% accurate**. What changed:

1. **Phase 2 shrank from "1–3 days, the core work" to "1–2 hours."** SX1262 support already
   exists via the CI-built `HELTEC_V3` env. This is a new-board port, not a new-chip port.
2. **Total estimate: 3–6 days → ~1–2 days.**
3. **Three of the draft's named risks are eliminated outright** (DIO interrupts, packet size
   limits, SX1276 timing constants) — see the table above.
4. **Three risks the draft did not name are now the top three** (TCXO, pin-map provenance,
   USB CDC).
5. **`BOARD_HAS_PSRAM` removed** — upstream explicitly does not set it.
6. **TCXO is a `begin()` parameter**, not a separate `setTCXO()` call, in this codebase.
7. **`RAK4631_ETH` is a trap** — despite the name, it is the wrong template (nRF52, Ethernet,
   probably doesn't compile).
8. **Phase 0's "skim the source" step is complete** and written up in §1.
9. Added an explicit CI-matrix step, which the draft omitted.

## Appendix B — Sources

- `jmead/Meshcore-Repeater-MQTT-Gateway` @ `b9c1a33` (main, 2025-11-20)
- `meshcore-dev/MeshCore` @ `03b6ef4` (main, 2026-07-28), `variants/rak3112/`
- `platformio/platform-espressif32` v6.11.0, `boards/esp32-s3-devkitc-1.json`
- RadioLib 6.6.0

Line numbers cited throughout refer to those commits and will drift.
