# Gateway Testing Implementation Complete

## What Has Been Created

A comprehensive testing suite for verifying the three core MeshCore MQTT Gateway features:

1. **Message Relaying/Forwarding** - Local LoRa packet repeating
2. **MQTT Backhaul** - Bridging LoRa packets over MQTT to distant gateways
3. **Advertisement Broadcasting** - Periodic adverts and neighbor discovery

## Files Created

### Test Scripts (`tools/`)

1. **`test_mqtt_backhaul.py`** ⭐ PRIMARY TEST
   - Monitors LoRa packets and verifies MQTT delivery
   - Identifies if only ADVERTs are reaching MQTT (current issue)
   - Tests bidirectional LoRa↔MQTT forwarding
   - Correlates packets between serial and MQTT

2. **`test_message_relay.py`**
   - Verifies local LoRa packet repeating
   - Checks deduplication functionality
   - Monitors relay statistics

3. **`test_advertisements.py`**
   - Validates periodic advert transmission
   - Checks MQTT publication of adverts
   - Monitors neighbor discovery
   - Measures advert intervals

4. **`test_access_control.py`**
   - Verifies denylist blocks specified nodes
   - Checks that allowed nodes pass through

5. **`run_all_tests.py`**
   - Orchestrator that runs all tests in sequence
   - Prompts for user confirmation between tests
   - Generates comprehensive report
   - Saves results to JSON file

6. **`quick_test.sh`**
   - Interactive script for easy test execution
   - Auto-detects serial port
   - Menu-driven test selection

### Documentation

1. **`TESTING_PROCEDURES.md`** 
   - Complete step-by-step manual testing procedures
   - Troubleshooting guide
   - Expected outputs for each test
   - Success criteria checklists

2. **`tools/TESTING_README.md`**
   - Quick reference for all test scripts
   - Usage examples
   - Common issues and solutions

3. **`test-gateway-features.plan.md`**
   - Overall test plan
   - Feature descriptions
   - Test requirements
   - Configuration notes

## Quick Start

### Option 1: Interactive Quick Test (Easiest)

```bash
cd tools
./quick_test.sh
```

The script will:
- Find your serial port automatically
- Ask for topic prefix
- Show menu of tests to run
- Execute your choice

### Option 2: Run PRIMARY Test Directly

```bash
cd tools
./test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

This is the most important test - it will show if ALL LoRa packets are forwarded to MQTT or just ADVERTs.

### Option 3: Run Full Test Suite

```bash
cd tools
./run_all_tests.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW
```

Runs all tests with user confirmation between each.

## Critical Issue Being Tested

### Current Problem

**Only ADVERT packets appear on MQTT**, but other LoRa packet types (text messages, binary data, etc.) do not reach the MQTT broker.

### What the Tests Will Show

The `test_mqtt_backhaul.py` script will:

1. Monitor serial output for ALL LoRa RX packets
2. Subscribe to MQTT topics (`/raw`, `/messages`, `/adverts`)
3. Correlate which LoRa packets appear on MQTT
4. Report statistics showing:
   - Total LoRa packets received
   - How many reached MQTT `/raw`
   - How many reached MQTT `/messages`
   - How many reached MQTT `/adverts`

**Expected Result (if bug exists):**
```
LoRa Packets:
  RX: 10
  TX: 5

MQTT Messages Received:
  /raw: 0          ⚠️ Should be > 0
  /messages: 0     ⚠️ Should be > 0
  /adverts: 3      ✓ Working

⚠ WARNING: Only ADVERTs forwarded to MQTT!
  - Non-ADVERT packets are NOT being forwarded
  - This is the main issue to fix
```

## Pre-Test Checklist

Before running tests, verify your LilyGo device:

- [ ] Powers on and shows serial output at 115200 baud
- [ ] WiFi configured and connected ("✓ WiFi connected")
- [ ] MQTT enabled and connected ("✓ MQTT connected")
- [ ] LoRa radio initialized ("✓ Radio listening for packets")
- [ ] Note your device's Node ID and Topic Prefix

**Check in serial monitor:**
```bash
# Using PlatformIO
pio device monitor -p /dev/cu.usbserial-XXXXX -b 115200

# Or screen
screen /dev/cu.usbserial-XXXXX 115200
```

Press `s` to view statistics, `c` to enter config menu.

## Test Duration Recommendations

- **Message Relay**: 60 seconds
- **MQTT Backhaul**: 90 seconds (PRIMARY TEST)
- **Advertisements**: 120 seconds (needs 2+ intervals)
- **Access Control**: 60 seconds

## What to Do With Results

### 1. Review Test Output

Each test provides:
- Real-time packet monitoring
- Correlation between LoRa and MQTT
- Success/failure analysis
- Specific recommendations

### 2. Check Generated Reports

```bash
ls tools/test_report_*.json
```

Reports contain:
- Timestamp and duration
- All test results
- Success/failure for each feature

### 3. If MQTT Backhaul Test Fails

**Symptoms**: Only ADVERTs appear on MQTT

**Next Steps:**

1. **Verify Configuration**
   ```
   Press 'c' in serial
   Press '2' for MQTT Settings
   Check: publishRaw = yes
   Check: publishDecoded = yes
   ```

2. **Check Code** (`src/main.cpp` lines 530-561)
   - Look at `handleLoRaPacket()` function
   - Verify `mqttHandler->publishRawPacket()` is called
   - Verify `mqttHandler->publishDecodedMessage()` is called
   - Check if conditions prevent publication

3. **MQTT Broker Logs**
   ```bash
   ssh root@mqtt.ripplenetworks.com.au
   tail -f /var/log/mosquitto/mosquitto.log
   ```

4. **Subscribe to MQTT Directly**
   ```bash
   mosquitto_sub -h mqtt.ripplenetworks.com.au -p 8883 \
     -u nswmesh -P nswmesh \
     -t "MESHCORE/AU/NSW/#" --insecure -v
   ```

### 4. Document Issues

If tests fail:
1. Save test report JSON
2. Capture serial output
3. Note exact failure symptoms
4. Create GitHub issue with details

## Gateway Function Reminder

### Repeater Function (Local)
```
MESHCORE Client ↔ LoRa RF ↔ Gateway ↔ LoRa RF ↔ MESHCORE Clients
```
- Extends local mesh range
- No MQTT involved

### Gateway Function (MQTT Backhaul)
```
Client ↔ LoRa ↔ Gateway (Sydney) ↔ MQTT Server ↔ Gateway (Broken Hill) ↔ LoRa ↔ Client
      MESHCORE/AU/NSW              (backhaul)             MESHCORE/AU/NSW
```
- Bridges distant mesh networks
- **Must forward ALL packet types, not just ADVERTs**

## Topic Hierarchy Behavior

### Parent Gateway (e.g., `MESHCORE/AU`)
- Subscribes to child topics: `MESHCORE/AU/NSW`, `MESHCORE/AU/VIC`, etc.
- Bridges child messages over local LoRa
- Extends network to distant regions

### Child Gateway (e.g., `MESHCORE/AU/NSW`)
- Only processes `MESHCORE/AU/NSW/*` topics
- Does NOT receive parent (`MESHCORE/AU`) messages
- Focuses on specific region

**Flow is directional**: Parent → Child bridging, NOT Child → Parent

## Success Criteria

### ✅ All Tests Pass When:

1. **Message Relay**
   - Packets repeated with proper deduplication
   - Statistics show forwarded count increasing

2. **MQTT Backhaul** ⭐
   - **ALL LoRa packet types appear on MQTT**
   - `/raw` receives all packets
   - `/messages` receives decoded packets
   - `/adverts` receives structured adverts
   - MQTT→LoRa forwarding works
   - Latency < 2 seconds

3. **Advertisements**
   - Periodic LoRa transmission at configured interval
   - Published to MQTT `/adverts`
   - Neighbor discovery functional

4. **Access Control**
   - Denylist blocks specified nodes
   - Allowed nodes pass through

## Troubleshooting Quick Reference

| Issue | Check | Fix |
|-------|-------|-----|
| No LoRa packets | Antenna, frequency, spreading factor | Config menu → LoRa Settings |
| Only ADVERTs on MQTT | `publishRaw`, `publishDecoded` settings | Config menu → MQTT Settings |
| MQTT not connecting | WiFi status, broker address, TLS/time | Check serial for errors |
| Packets not repeated | `maxHops` setting | Config menu → Repeater Settings |
| Serial port not found | USB connection, permissions | Check `ls /dev/cu.usbserial-*` |

## Next Steps

1. **Run the PRIMARY test** (`test_mqtt_backhaul.py`)
2. **Identify if only ADVERTs reach MQTT**
3. **If bug confirmed**, investigate code in `src/main.cpp` around lines 530-561
4. **Fix the issue** to forward all packet types
5. **Re-run tests** to verify fix
6. **Update documentation** with findings

## Support

- **Serial Output**: Always capture full serial output when debugging
- **MQTT Logs**: SSH access available to check broker logs
- **GitHub Issues**: Report bugs with test results attached
- **MeshCore Discord**: Community support available

## Files for Reference

- Test Plan: `test-gateway-features.plan.md`
- Manual Procedures: `TESTING_PROCEDURES.md`
- Tool Documentation: `tools/TESTING_README.md`
- Source Code: `src/main.cpp`, `src/mqtt_handler.h`
- Configuration: `src/config.h`

---

**Ready to test!** Start with `cd tools && ./quick_test.sh`

