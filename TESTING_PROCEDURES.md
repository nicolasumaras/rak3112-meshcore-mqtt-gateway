# Gateway Testing Procedures

Complete step-by-step manual testing procedures for MeshCore MQTT Gateway features.

## Table of Contents

1. [Pre-Test Setup](#pre-test-setup)
2. [Test 1: Message Relaying/Forwarding](#test-1-message-relayingforwarding)
3. [Test 2: MQTT Backhaul (PRIMARY)](#test-2-mqtt-backhaul-primary)
4. [Test 3: Advertisement Broadcasting](#test-3-advertisement-broadcasting)
5. [Test 4: Access Control](#test-4-access-control)
6. [Running Automated Tests](#running-automated-tests)
7. [Troubleshooting](#troubleshooting)

---

## Pre-Test Setup

### Hardware Requirements

- **LilyGo LoRa32 V2.1** with latest firmware installed
- USB cable connected to computer
- LoRa antenna attached
- (Optional) Second LoRa device for testing relay function

### Software Requirements

```bash
# Install Python dependencies
pip install paho-mqtt pyserial
```

### Step 1: Find Serial Port

**macOS:**
```bash
ls /dev/cu.usbserial-*
```

**Linux:**
```bash
ls /dev/ttyUSB*
```

Expected output: `/dev/cu.usbserial-0001` (or similar)

### Step 2: Connect to Serial Monitor

```bash
# Using PlatformIO
pio device monitor -p /dev/cu.usbserial-XXXXX -b 115200

# Or using screen
screen /dev/cu.usbserial-XXXXX 115200
```

### Step 3: Verify Device Status

In the serial monitor, check for:

```
✓ Configuration loaded
✓ LoRa initialized
✓ WiFi connected
✓ MQTT connected
```

If any are missing, press `c` to enter configuration menu and configure:
- **Option 1**: WiFi Settings
- **Option 2**: MQTT Settings
- **Option 6**: Save Configuration
- **Option 8**: Restart Device

### Step 4: Note Device Configuration

Press `c` then `5` to view current configuration. Record:

- **Node Name**: ________________
- **Node ID**: ________________
- **Topic Prefix**: ________________ (e.g., `MESHCORE/AU/NSW`)
- **LoRa Frequency**: ________________
- **Max Hops**: ________________
- **Advert Enabled**: ________________

Press `0` twice to exit configuration menu.

---

## Test 1: Message Relaying/Forwarding

**Objective**: Verify that received LoRa packets are retransmitted to extend network range.

### Manual Testing

#### Step 1.1: Check Repeater Settings

1. Press `c` to enter config menu
2. Press `4` for Repeater Settings
3. Verify `Max Hops` > 0 (e.g., 3)
4. Press `0` twice to exit

#### Step 1.2: Monitor for Relay Events

Watch serial output for these patterns:

```
📡 LoRa RX: 32 bytes | RSSI: -85 dBm | SNR: 8.5 dB
   Data: 41 44 56 45 52 54 ...
   Text: "ADVERT 12345678 Node-1 0.0 0.0"
   ↻ Packet repeated
```

#### Step 1.3: Verify Deduplication

If the same packet is received twice within 2 seconds:

```
📡 LoRa RX: 32 bytes | RSSI: -85 dBm | SNR: 8.5 dB
   ...
   ↻ Skipped repeat (duplicate seen recently)
```

#### Step 1.4: Check Statistics

Press `s` to view statistics:

```
Packets Received: 15
Packets Sent:     8
Packets Forwarded: 12
```

### Expected Results

- ✓ LoRa packets are received (RX count increases)
- ✓ Packets are retransmitted (see "↻ Packet repeated")
- ✓ Duplicates are detected and skipped
- ✓ Forwarded count increases in statistics

### Automated Testing

```bash
cd tools
python3 test_message_relay.py --serial /dev/cu.usbserial-XXXXX --duration 60
```

---

## Test 2: MQTT Backhaul (PRIMARY)

**Objective**: **Verify ALL LoRa packets are forwarded to MQTT, not just ADVERTs.**

This is the most critical test. Currently only ADVERT packets may be reaching MQTT.

### Manual Testing

#### Step 2.1: Subscribe to MQTT Topics

Open a new terminal and subscribe to all topics:

```bash
# Option A: Using mosquitto_sub
mosquitto_sub -h mqtt.ripplenetworks.com.au -p 8883 \
  -u nswmesh -P nswmesh \
  -t "MESHCORE/AU/NSW/#" --insecure -v

# Option B: Using our helper script
cd tools
python3 mqtt_subscribe.py \
  --host mqtt.ripplenetworks.com.au --port 8883 \
  --username nswmesh --password nswmesh \
  --prefix MESHCORE/AU/NSW --insecure
```

Keep this terminal open to monitor MQTT messages.

#### Step 2.2: Test LoRa → MQTT Flow

**Watch serial monitor for LoRa RX:**

```
📡 LoRa RX: 32 bytes | RSSI: -85 dBm | SNR: 8.5 dB
   Data: 48 65 6C 6C 6F
   Text: "Hello"
```

**Check MQTT terminal for corresponding messages:**

Expected on MQTT:

1. **Raw packet** on `MESHCORE/AU/NSW/raw`:
   ```json
   {
     "timestamp": 12345678,
     "rssi": -85,
     "snr": 8.5,
     "data": "48656C6C6F",
     "length": 5,
     "gateway": "MQTT-Gateway"
   }
   ```

2. **Decoded message** on `MESHCORE/AU/NSW/messages`:
   ```json
   {
     "timestamp": 12345678,
     "from": 305419896,
     "to": 4294967295,
     "message": "Hello",
     "type": 0,
     "rssi": -85,
     "snr": 8.5,
     "hops": 0,
     "gateway": "MQTT-Gateway"
   }
   ```

#### Step 2.3: Test MQTT → LoRa Flow

**Publish a test message to MQTT:**

```bash
# From another terminal
mosquitto_pub -h mqtt.ripplenetworks.com.au -p 8883 \
  -u nswmesh -P nswmesh \
  -t "MESHCORE/AU/NSW/commands/send" \
  -m "TEST MESSAGE" --insecure
```

**Watch serial monitor for LoRa TX:**

```
MQTT message received: MESHCORE/AU/NSW/commands/send
Forwarding MQTT message to LoRa (12 bytes)
📤 LoRa TX: 12 bytes
   ✓ Sent successfully
```

#### Step 2.4: Verify Packet Type Coverage

Check that MQTT receives **all** packet types:

| Packet Type | LoRa RX Detected | MQTT `/raw` Received | MQTT `/messages` Received |
|-------------|------------------|---------------------|---------------------------|
| Text message | ☐ | ☐ | ☐ |
| ADVERT | ☐ | ☐ | ☐ |
| Binary data | ☐ | ☐ | ☐ |

**⚠ CRITICAL ISSUE**: If only ADVERTs appear on MQTT but other packets don't, the gateway is not properly forwarding all packet types.

### Expected Results

- ✓ **ALL** LoRa RX packets appear on MQTT `/raw` topic
- ✓ Printable packets appear on MQTT `/messages` topic
- ✓ ADVERT packets appear on MQTT `/adverts` topic
- ✓ MQTT messages published to `/commands/send` are transmitted via LoRa
- ✓ Latency LoRa→MQTT < 2 seconds

### Automated Testing

```bash
cd tools
python3 test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --mqtt-host mqtt.ripplenetworks.com.au \
  --mqtt-port 8883 \
  --mqtt-user nswmesh \
  --mqtt-pass nswmesh \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

This script will:
- Monitor serial for LoRa packets
- Subscribe to MQTT topics
- Correlate LoRa packets with MQTT messages
- Report which packet types are being forwarded
- **Identify if only ADVERTs are reaching MQTT**

---

## Test 3: Advertisement Broadcasting

**Objective**: Verify periodic advertisement transmission and neighbor discovery.

### Manual Testing

#### Step 3.1: Enable Advertisements

1. Press `c` to enter config menu
2. Press `11` for Discovery Settings
3. Enable adverts: `y`
4. Set interval (e.g., 60 seconds)
5. Press `0` to save
6. Press `10` for Location Settings (optional)
7. Set latitude/longitude
8. Press `0` twice to exit and save
9. Press `8` to restart device

#### Step 3.2: Monitor Advertisement Transmission

Watch serial output:

```
📤 LoRa TX: 48 bytes
   ✓ Sent successfully
```

Check that this happens at the configured interval (e.g., every 60 seconds).

#### Step 3.3: Verify MQTT Publication

In the MQTT subscriber terminal, watch for:

```
MESHCORE/AU/NSW/adverts {
  "timestamp": 12345678,
  "nodeId": 305419896,
  "name": "MQTT-Gateway",
  "lat": -33.86,
  "lon": 151.21,
  "gateway": "MQTT-Gateway"
}
```

#### Step 3.4: Check Neighbor Discovery

If another LoRa device transmits an advert, you should see:

```
📡 LoRa RX: 45 bytes | RSSI: -75 dBm | SNR: 9.2 dB
   Text: "ADVERT 87654321 Remote-Node -33.86 151.21"
   ✓ Neighbour updated from advert
```

Press `n` to view neighbor table:

```
ID: 0x87654321  Name: Remote-Node     RSSI: -75  SNR: 9.2  Age: 15s  Lat: -33.86  Lon: 151.21
```

### Expected Results

- ✓ Gateway transmits adverts at configured interval
- ✓ Adverts published to MQTT `/adverts` topic
- ✓ Received adverts populate neighbor table
- ✓ Neighbor table viewable with `n` command

### Automated Testing

```bash
cd tools
python3 test_advertisements.py \
  --serial /dev/cu.usbserial-XXXXX \
  --mqtt-host mqtt.ripplenetworks.com.au \
  --mqtt-port 8883 \
  --mqtt-user nswmesh \
  --mqtt-pass nswmesh \
  --prefix MESHCORE/AU/NSW \
  --duration 120
```

---

## Test 4: Access Control

**Objective**: Verify denylist blocks adverts from specified nodes.

### Manual Testing

#### Step 4.1: Configure Denylist

1. Press `c` to enter config menu
2. Press `12` for Access Control
3. Enable denylist: `y`
4. Enter node IDs to deny (hex format, e.g., `12345678`)
5. Press `0` three times to save and exit
6. Press `8` to restart

#### Step 4.2: Test Denied Node

If a denied node transmits an advert:

```
📡 LoRa RX: 45 bytes | RSSI: -75 dBm | SNR: 9.2 dB
   Text: "ADVERT 12345678 BadNode 0.0 0.0"
   ✗ Advert dropped (denied node)
```

The advert should NOT appear in:
- Neighbor table (`n` command)
- MQTT `/adverts` topic

#### Step 4.3: Test Allowed Node

An allowed node's advert should pass through normally:

```
📡 LoRa RX: 45 bytes | RSSI: -75 dBm | SNR: 9.2 dB
   Text: "ADVERT 87654321 GoodNode 0.0 0.0"
   ✓ Neighbour updated from advert
```

### Expected Results

- ✓ Denied nodes' adverts are dropped
- ✓ Denied nodes do NOT appear in neighbor table
- ✓ Denied nodes' adverts do NOT reach MQTT
- ✓ Allowed nodes pass through normally

### Automated Testing

```bash
cd tools
python3 test_access_control.py --serial /dev/cu.usbserial-XXXXX --duration 60
```

---

## Running Automated Tests

### Quick Test (Single Feature)

```bash
# Test MQTT backhaul only (PRIMARY)
python3 tools/test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

### Full Test Suite

```bash
python3 tools/run_all_tests.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --relay-duration 60 \
  --backhaul-duration 90 \
  --advert-duration 120 \
  --access-duration 60
```

The orchestrator will:
1. Prompt you before each test
2. Run all tests in sequence
3. Generate a comprehensive report
4. Save results to JSON file

### Skip Specific Tests

```bash
# Skip access control test
python3 tools/run_all_tests.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --skip access
```

---

## Troubleshooting

### Issue: No LoRa Packets Received

**Symptoms**: `Packets Received: 0` in statistics

**Checks**:
1. Verify LoRa antenna is connected
2. Check LoRa frequency matches other devices
3. Verify spreading factor and bandwidth match network
4. Check sync word (default: 0x34)
5. Look for "✓ Radio listening for packets" in serial output

**Fix**:
```bash
# In serial monitor
c  # Enter config
3  # LoRa Settings
# Verify all settings match your network
0  # Exit
6  # Save
8  # Restart
```

### Issue: Only ADVERTs Appear on MQTT

**Symptoms**: `/adverts` topic receives messages, but `/raw` and `/messages` are empty

**This is the main issue to fix!**

**Checks**:
1. Press `c`, then `2` for MQTT Settings
2. Verify `Publish Raw: yes`
3. Verify `Publish Decoded: yes`
4. Check MQTT connection: Look for "✓ MQTT connected" in serial

**Possible causes**:
- `publishRaw` or `publishDecoded` disabled in config
- MQTT connection dropping and reconnecting
- Code only publishes adverts, not all packet types
- Conditional logic preventing non-advert publication

**Debug**:
```bash
# Check MQTT broker logs
ssh root@mqtt.ripplenetworks.com.au
tail -f /var/log/mosquitto/mosquitto.log
```

### Issue: MQTT Not Connecting

**Symptoms**: "✗ MQTT connection failed"

**Checks**:
1. Verify WiFi connected first
2. Check MQTT broker address and port
3. Verify username/password
4. For TLS: Check time is set via NTP

**Fix**:
```bash
# In serial monitor
c  # Enter config
1  # WiFi Settings - verify connected
2  # MQTT Settings - verify all details
13 # Clock/NTP - trigger time sync
0  # Exit
6  # Save
8  # Restart
```

### Issue: Packets Not Being Relayed

**Symptoms**: LoRa RX occurs but no "↻ Packet repeated"

**Checks**:
1. `Max Hops` setting (must be > 0)
2. Check for "↻ Skipped repeat (duplicate seen recently)" (deduplication working)

**Fix**:
```bash
c  # Enter config
4  # Repeater Settings
# Set Max Hops to 3
0  # Exit
6  # Save
8  # Restart
```

### Issue: Test Scripts Can't Find Serial Port

**Symptoms**: "Serial connection failed"

**macOS**:
```bash
ls -l /dev/cu.usbserial-*
# Try unplugging and replugging USB cable
```

**Linux**:
```bash
ls -l /dev/ttyUSB*
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Logout and login again
```

### Issue: Python Dependencies Missing

**Symptoms**: "ModuleNotFoundError: No module named 'paho'"

**Fix**:
```bash
pip install paho-mqtt pyserial

# Or create virtual environment
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install paho-mqtt pyserial
```

---

## Success Criteria Summary

### ✓ Message Relaying/Forwarding
- Packets received and retransmitted locally
- Deduplication prevents loops
- Statistics show forwarded count increasing

### ✓ MQTT Backhaul (CRITICAL)
- **ALL** LoRa packet types forwarded to MQTT, not just ADVERTs
- `/raw` topic receives all packets
- `/messages` topic receives decoded packets
- `/adverts` topic receives structured adverts
- MQTT→LoRa direction works for commands
- Latency < 2 seconds

### ✓ Advertisement Broadcasting
- Periodic adverts transmitted over LoRa
- Adverts published to MQTT
- Neighbor discovery functional
- Configurable interval respected

### ✓ Access Control
- Denylist blocks specified node IDs
- Denied adverts do not reach neighbor table or MQTT
- Allowed nodes pass through normally

---

## Next Steps

After completing all tests:

1. **Review test reports** in `tools/test_report_*.json`
2. **Fix any failing tests** based on troubleshooting section
3. **Document any issues** found in GitHub issues
4. **Update firmware** if bugs discovered
5. **Repeat tests** after fixes to verify resolution

For support:
- Check existing issues in GitHub repository
- Join MeshCore Discord for community help
- Review serial output and MQTT logs for debugging

