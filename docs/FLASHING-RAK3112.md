# Flashing the RAK3112

## Status of this firmware

**Flashed and running on a real RAK3112. The radio initializes and transmits.** What is not
yet proven is anything needing a second device or a network — see
[What is and isn't proven](#what-is-and-isnt-proven).

### Hardware this was verified on

```
Chip           ESP32-S3 (QFN56) rev v0.2
Features       WiFi, BLE, Embedded PSRAM 8MB (AP_3v3)
Crystal        40MHz
USB mode       USB-Serial/JTAG
Flash          16MB, quad (set in eFuse), 3.3V
Port           /dev/cu.usbmodem*  (VID:PID 303A:1001)
```

Two mismatches with the build config, both deliberate and both harmless:

- The chip has **8 MB PSRAM**; the build does not enable it. Upstream MeshCore does not
  either. The firmware uses 15.5% of internal RAM, so there is no pressure to change this.
- The chip has **16 MB flash**; the build uses the 8 MB `esp32-s3-devkitc-1` layout, so the
  upper 8 MB is unused. Again matching upstream MeshCore.

Both are worth revisiting only if you actually run short of space. Changing partition layout
during bring-up adds variables at exactly the wrong moment.

## Build

PlatformIO is required. If you don't have it:

```bash
python3 -m venv piovenv
./piovenv/bin/pip install platformio
```

Then:

```bash
pio run -e rak3112_mqtt
```

Expected: `SUCCESS`, RAM ~15.5%, Flash ~28.4%.

Artifacts land in `.pio/build/rak3112_mqtt/`:

| File | Offset |
|---|---|
| `bootloader.bin` | `0x0` |
| `partitions.bin` | `0x8000` |
| `firmware.bin` | `0x10000` |

## Flash

With the board connected over USB:

```bash
pio run -e rak3112_mqtt -t upload
```

To pick the port explicitly:

```bash
pio run -e rak3112_mqtt -t upload --upload-port /dev/cu.usbmodem????
```

If upload fails to start, put the board in download mode: hold **BOOT**, tap **RESET**,
release **BOOT**.

If `921600` proves flaky on your cable/adapter, drop `upload_speed` in `platformio.ini`
to `460800`.

## First boot

```bash
pio device monitor -e rak3112_mqtt
```

115200 baud. The ESP32-S3 enumerates as native USB CDC, so the port may take a second or two
to appear after reset — and may re-enumerate on reset. That is normal.

You should see the radio init line. **`Initializing radio... success!` is the thing to look
for** — it means the SX1262 was found on the ported pin map, which is the single most
important unknown in this port.

Then use the interactive serial menu to set WiFi SSID/password, MQTT broker, and credentials.

## If it fails

### `Initializing radio... ` followed by a failure code

**Code `-2` (`RADIOLIB_ERR_CHIP_NOT_FOUND`)** is the expected failure mode if something is
wrong. Triage in this order — see [issue #11](../../issues/11):

1. **TCXO voltage.** `1.8` V is what upstream MeshCore uses. Set in `src/main.cpp`, the
   `RAK_3112` branch of `setupLoRa()`.
2. **Pin map.** Verified against `meshcore-dev/MeshCore` @ `03b6ef4` — but that variant has
   **no CI coverage** upstream, so the values are community-contributed and not continuously
   validated.
3. **VEXT / GPIO14.** Upstream leaves it low and the radio works, so this *should* be
   unnecessary — but that is verified software behavior, not a schematic reading. Try
   `pinMode(14, OUTPUT); digitalWrite(14, HIGH);` before `SPI.begin()` in `setupLoRa()`.
4. SPI wiring and bus speed.

### No serial output at all

Confirm `-DARDUINO_USB_CDC_ON_BOOT=1` is present in the env. Without it there is no console
on ESP32-S3 native USB — and no console means no way to configure WiFi or MQTT, so the board
looks dead while being fine. See [issue #8](../../issues/8).

### Radio settings changed in the menu had no effect

Expected. `setupLoRa()` runs once from `setup()` — **reboot after changing LoRa parameters**.
This is upstream behavior, not something this port introduced.

## What is and isn't proven

**Verified:**

- `pio run -e rak3112_mqtt` succeeds; `firmware.bin` (~950 KB), `bootloader.bin`, and
  `partitions.bin` are produced.
- The **SX1262** path compiled, not SX1276 — `nm` on `firmware.elf` shows 10 `SX1262` and
  99 `SX126x` symbols and **zero** `SX1276`. This matters because a wrong `-D` would have
  silently built the generic SX1276 path and still reported success.
- `setDio2AsRfSwitch`, `setCurrentLimit`, `setRxBoostedGainMode`, and `setTCXO` are all
  linked into the image.
- All eight RAK3112 pin values compiled through as intended, checked with temporary
  `static_assert`s. The probe was negative-controlled — a deliberately wrong value **did**
  fail the build — so the pass is meaningful rather than vacuous. The probe was removed
  afterwards; `src/main.cpp` is byte-identical to its pre-probe state.

**Verified on hardware:**

- Flashes successfully; `Hash of data verified`.
- **The SX1262 initializes** — `Initializing radio... success!`, then
  `✓ Radio listening for packets`. The ported pin map and the 1.8 V TCXO setting are
  correct on real silicon.
- **GPIO14/VEXT is not needed for the radio.** It was never driven and the radio works,
  confirming the inference drawn from upstream MeshCore's software behaviour.
- **The USB CDC console enumerates** and the interactive menu responds — so the
  `ARDUINO_USB_CDC_ON_BOOT=1` decision was right.
- **Transmit works** — the `t` self-test reports `LoRa TX: 16 bytes / ✓ Sent successfully`,
  and the radio returns to `RX ACTIVE` afterwards.
- The radio reports `Radio Initialized: YES` and stays in RX between operations.

**Still not verified:**

- **Over-the-air RX.** No second MeshCore node was in range, so no real packet has ever been
  received. The RX path is *armed* and demonstrably interrupt-driven, but has not carried a
  real packet.
- **That a transmitted packet is actually receivable.** TX is confirmed only from the
  sender's side — the radio accepted and sent it. Nothing has heard it.
- **MQTT, in either direction.** WiFi and MQTT are unconfigured; the boot banner reports
  `WiFi: Disabled` / `MQTT: Disabled`.
- **Sustained operation** — reconnect handling, heap behaviour over days.

Phases 3-5 in [PORTING-PLAN.md](PORTING-PLAN.md) close exactly these gaps. All of them need
either a second radio or network credentials.

### One reading not to chase

`Current RSSI: -5 dBm` in the debug output is **not** a noise floor. RadioLib's `getRSSI()`
on SX126x returns the *last received packet's* RSSI, and no packet has ever been received, so
the value is uninitialized. It is not evidence of a stuck receiver.

## Upstreaming

The port is isolated in one commit on top of a pristine vendor commit, so the patch to offer
`jmead` is:

```bash
git diff e0e3f79..HEAD -- src platformio.ini .github
```

See [issue #14](../../issues/14) for what to say in the PR.
