# ✅ Gateway Testing Implementation Complete

## Summary

A comprehensive testing suite has been implemented to verify the three core MeshCore MQTT Gateway features:

1. **Message Relaying/Forwarding** - Local LoRa packet repeating to extend mesh range
2. **MQTT Backhaul** - Bridging LoRa over MQTT to connect distant networks ⭐ PRIMARY FOCUS
3. **Advertisement Broadcasting** - Periodic node adverts and neighbor discovery

## 🎯 Critical Focus: MQTT Backhaul

**Current Issue**: Only ADVERT packets may be reaching MQTT; other packet types (text messages, binary data) might not be forwarded.

**Solution**: The `test_mqtt_backhaul.py` script will identify this issue by:
- Monitoring ALL LoRa packets received
- Verifying which packets reach MQTT
- Reporting gaps in forwarding
- Pinpointing if only ADVERTs are being forwarded

## 📁 What Was Created

### Test Scripts (tools/)

| File | Purpose | Duration |
|------|---------|----------|
| `test_mqtt_backhaul.py` ⭐ | Verify ALL LoRa packets forwarded to MQTT | 90s |
| `test_message_relay.py` | Verify local packet repeating | 60s |
| `test_advertisements.py` | Verify periodic adverts | 120s |
| `test_access_control.py` | Verify denylist blocking | 60s |
| `run_all_tests.py` | Orchestrator for all tests | Variable |
| `quick_test.sh` | Interactive test launcher | Variable |

### Documentation

| File | Purpose |
|------|---------|
| `HOW_TO_TEST.md` | ⭐ **START HERE** - Quick start guide |
| `TEST_SUMMARY.md` | Overview of implementation |
| `TESTING_PROCEDURES.md` | Detailed step-by-step procedures |
| `tools/TESTING_README.md` | Test script reference |
| `test-gateway-features.plan.md` | Complete test plan |

## 🚀 How to Start Testing

### Option 1: Interactive (Easiest)

```bash
cd "/Users/jasonmead/Meshcore Repeater MQTT/tools"
./quick_test.sh
```

### Option 2: Run Primary Test Directly

```bash
cd "/Users/jasonmead/Meshcore Repeater MQTT/tools"

# Replace XXXXX with your serial port number
./test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

### Option 3: Full Test Suite

```bash
cd "/Users/jasonmead/Meshcore Repeater MQTT/tools"

./run_all_tests.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW
```

## 📋 Pre-Test Checklist

Before running tests:

1. **Find your serial port:**
   ```bash
   ls /dev/cu.usbserial-*    # macOS
   ls /dev/ttyUSB*           # Linux
   ```

2. **Install dependencies:**
   ```bash
   pip install paho-mqtt pyserial
   ```

3. **Verify device is working:**
   ```bash
   # Connect to serial monitor
   pio device monitor -p /dev/cu.usbserial-XXXXX -b 115200
   
   # Check for:
   # ✓ WiFi connected
   # ✓ MQTT connected
   # ✓ Radio listening for packets
   ```

4. **Note your configuration:**
   - Serial Port: `/dev/cu.usbserial-_____`
   - Topic Prefix: `MESHCORE/AU/NSW` (or your prefix)
   - Node ID: (shown in serial output)

## 🔍 What the Tests Will Show

### If MQTT Backhaul Test Passes ✅

```
MQTT Messages Received:
  /raw: 10         ✓ All packets forwarded
  /messages: 8     ✓ Decoded packets forwarded
  /adverts: 3      ✓ Adverts forwarded

✓ ALL TESTS PASSED

Gateway Features Verified:
  ✓ Message Relaying/Forwarding
  ✓ MQTT Backhaul (Network Extension)
  ✓ Advertisement Broadcasting
```

### If MQTT Backhaul Test Fails ❌

```
LoRa Packets:
  RX: 10

MQTT Messages Received:
  /raw: 0          ✗ NONE forwarded
  /messages: 0     ✗ NONE forwarded
  /adverts: 3      ✓ Only adverts forwarded

⚠ WARNING: Only ADVERTs forwarded to MQTT!
  - Non-ADVERT packets are NOT being forwarded
  - This is the main issue to fix
```

## 🔧 Next Steps Based on Results

### If Tests Pass

1. ✅ Gateway fully functional
2. Document working configuration
3. Deploy additional gateways using same settings

### If Only ADVERTs Reach MQTT

This indicates a bug in the forwarding logic. Here's how to debug:

1. **Check Configuration**
   ```
   In serial monitor:
   Press 'c' → '2' (MQTT Settings)
   Verify: publishRaw = yes
   Verify: publishDecoded = yes
   ```

2. **Review Code**
   
   Open `src/main.cpp` and check `handleLoRaPacket()` function (lines 428-581):
   
   ```cpp
   // Lines 530-561: MQTT forwarding logic
   if (mqttHandler && mqttHandler->isConnected()) {
       // Check if this code is being executed
       
       // Publish raw packet
       if (config.mqtt.publishRaw) {
           mqttHandler->publishRawPacket(data, length, rssi, snr);
       }
       
       // Publish decoded message
       if (config.mqtt.publishDecoded && isPrintable) {
           mqttHandler->publishDecodedMessage(...);
       }
   }
   ```
   
   **Possible issues:**
   - Conditions preventing execution
   - MQTT connection dropping
   - `publishRaw`/`publishDecoded` disabled
   - Logic only calls publish functions for adverts

3. **Add Debug Logging**
   
   Add serial prints to verify execution:
   ```cpp
   Serial.println("MQTT forwarding check:");
   Serial.printf("  Connected: %d\n", mqttHandler->isConnected());
   Serial.printf("  publishRaw: %d\n", config.mqtt.publishRaw);
   Serial.printf("  publishDecoded: %d\n", config.mqtt.publishDecoded);
   ```

4. **Check MQTT Broker Logs**
   ```bash
   ssh root@mqtt.ripplenetworks.com.au
   tail -f /var/log/mosquitto/mosquitto.log
   ```

5. **Test MQTT Directly**
   ```bash
   # Subscribe and watch for messages
   mosquitto_sub -h mqtt.ripplenetworks.com.au -p 8883 \
     -u nswmesh -P nswmesh \
     -t "MESHCORE/AU/NSW/#" --insecure -v
   ```

## 📊 Test Reports

Each test run generates:

1. **Console Output** - Real-time results
2. **JSON Report** - `tools/test_report_YYYYMMDD_HHMMSS.json`

Example report location:
```
/Users/jasonmead/Meshcore Repeater MQTT/tools/test_report_20251012_193000.json
```

Contains:
- Start/end timestamps
- Serial port used
- MQTT configuration
- Results for each test
- Success/failure status

## 🎓 Understanding Gateway Functions

### 1. Repeater Function (Local LoRa)

```
Client A ↔ LoRa ↔ Gateway ↔ LoRa ↔ Client B
         (within radio range)
```

- Extends mesh range by repeating packets
- All local - no MQTT involved
- Tested by: `test_message_relay.py`

### 2. Gateway Function (MQTT Backhaul)

```
Client (Sydney) ↔ LoRa ↔ Gateway (Sydney)
                          ↓ MQTT
                    Internet/Network
                          ↓ MQTT
              Gateway (Broken Hill) ↔ LoRa ↔ Client (Broken Hill)
```

- Bridges distant mesh networks over MQTT
- **Must forward ALL packet types**
- Enables communication beyond radio range
- Tested by: `test_mqtt_backhaul.py` ⭐

### 3. Advertisement System

```
Gateway → Periodic LoRa broadcasts (ADVERT messages)
       → Publishes to MQTT /adverts topic
       → Receives adverts from other nodes
       → Builds neighbor table
```

- Announces presence to network
- Enables node discovery
- Publishes location data
- Tested by: `test_advertisements.py`

## 🌍 Topic Hierarchy

### Parent Gateway (`MESHCORE/AU`)

- Subscribes to: `MESHCORE/AU/NSW/#`, `MESHCORE/AU/VIC/#`, etc.
- Bridges child region messages over local LoRa
- Extends network across regions

### Child Gateway (`MESHCORE/AU/NSW`)

- Subscribes to: `MESHCORE/AU/NSW/#` only
- Does NOT receive parent messages
- Focused on specific region

**Flow**: Parent → Child (one direction)

## 🛟 Support Resources

- **Documentation**: See `HOW_TO_TEST.md` for quick start
- **Manual Procedures**: See `TESTING_PROCEDURES.md` for detailed steps
- **Script Reference**: See `tools/TESTING_README.md` for all commands
- **GitHub Issues**: Report bugs with test outputs attached
- **MeshCore Discord**: Community support
- **MQTT Broker**: SSH access for log review

## 🎯 Success Criteria Summary

| Feature | Test Script | Success Criteria |
|---------|-------------|------------------|
| Message Relay | `test_message_relay.py` | Packets repeated, deduplication works |
| MQTT Backhaul ⭐ | `test_mqtt_backhaul.py` | ALL packets forwarded to MQTT |
| Advertisements | `test_advertisements.py` | Periodic LoRa TX, MQTT publication |
| Access Control | `test_access_control.py` | Denylist blocks specified nodes |

## 📞 Getting Help

If tests fail:

1. **Save all output** (console + JSON report)
2. **Capture serial log** (full session)
3. **Note exact failure** (which test, what symptoms)
4. **Check troubleshooting** in `TESTING_PROCEDURES.md`
5. **Create GitHub issue** with:
   - Test output
   - Serial log
   - Configuration (from `c` → `5`)
   - Device details (LilyGo model, firmware version)

## ✨ You're Ready!

Everything is set up and ready to test. Start here:

```bash
cd "/Users/jasonmead/Meshcore Repeater MQTT/tools"
./quick_test.sh
```

Or read the quick start guide:

```bash
open "HOW_TO_TEST.md"
```

---

**Implementation Complete** ✅

All test scripts, documentation, and procedures are in place. You can now verify your gateway's three core features and identify if ALL LoRa packet types are being forwarded to MQTT.

**Primary Focus**: Run `test_mqtt_backhaul.py` to check if only ADVERTs are reaching MQTT.

Good luck with testing! 🚀

