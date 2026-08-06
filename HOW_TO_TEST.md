# How to Test Your Gateway - START HERE

## ⚡ Quick Start (5 minutes)

### 1. Connect Your LilyGo Device

```bash
# Find your serial port
ls /dev/cu.usbserial-*    # macOS
ls /dev/ttyUSB*           # Linux
```

### 2. Install Dependencies

```bash
pip install paho-mqtt pyserial
```

### 3. Run the Interactive Test

```bash
cd tools
./quick_test.sh
```

The script will:
- Auto-detect your serial port
- Ask for your MQTT topic prefix (e.g., `MESHCORE/AU/NSW`)
- Show a menu of tests
- Run your selected test

## 🎯 Most Important Test

### MQTT Backhaul Test (PRIMARY)

**This test identifies if ALL LoRa packets reach MQTT or just ADVERTs.**

```bash
cd tools
./test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

**What it checks:**
- ✓ LoRa packets received
- ✓ ALL packets forwarded to MQTT `/raw`
- ✓ Decoded packets on MQTT `/messages`
- ✓ Adverts on MQTT `/adverts`
- ✓ MQTT→LoRa forwarding works
- ✓ Packet correlation and timing

**Current Issue**: Only ADVERT packets may be reaching MQTT. This test will confirm.

## 📚 Full Documentation

| Document | Purpose |
|----------|---------|
| `TEST_SUMMARY.md` | Overview of all tests and what was created |
| `TESTING_PROCEDURES.md` | Step-by-step manual testing procedures |
| `tools/TESTING_README.md` | Reference for all test scripts |
| `test-gateway-features.plan.md` | Complete test plan |

## 🧪 All Available Tests

### 1. Message Relay Test (60 seconds)

Tests local LoRa packet repeating:

```bash
./test_message_relay.py --serial /dev/cu.usbserial-XXXXX --duration 60
```

### 2. MQTT Backhaul Test (90 seconds) ⭐ PRIMARY

Tests LoRa↔MQTT bridging:

```bash
./test_mqtt_backhaul.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 90
```

### 3. Advertisement Test (120 seconds)

Tests periodic adverts and neighbor discovery:

```bash
./test_advertisements.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW \
  --duration 120
```

### 4. Access Control Test (60 seconds)

Tests denylist functionality:

```bash
./test_access_control.py --serial /dev/cu.usbserial-XXXXX --duration 60
```

### 5. Full Test Suite

Runs all tests with prompts:

```bash
./run_all_tests.py \
  --serial /dev/cu.usbserial-XXXXX \
  --prefix MESHCORE/AU/NSW
```

## 🔍 What to Look For

### ✅ Success Indicators

**Serial Output:**
```
✓ LoRa initialized
✓ WiFi connected
✓ MQTT connected
📡 LoRa RX: 32 bytes | RSSI: -85 dBm | SNR: 8.5 dB
📤 LoRa TX: 32 bytes
   ✓ Sent successfully
```

**MQTT Output:**
```
MESHCORE/AU/NSW/raw -> {"data":"48656C6C6F", "rssi":-85, ...}
MESHCORE/AU/NSW/messages -> {"message":"Hello", "from":305419896, ...}
MESHCORE/AU/NSW/adverts -> {"nodeId":305419896, "name":"Gateway", ...}
```

### ❌ Problem Indicators

**Only ADVERTs on MQTT:**
```
MQTT Messages Received:
  /raw: 0          ⚠️ Should be > 0
  /messages: 0     ⚠️ Should be > 0
  /adverts: 5      ✓ Working

⚠ WARNING: Only ADVERTs forwarded to MQTT!
```

**No LoRa Traffic:**
```
Packets Received: 0
⚠ WARNING: No LoRa RX packets detected
```

**MQTT Not Connected:**
```
✗ MQTT connection failed
✗ No MQTT messages received
```

## 🛠️ Pre-Test Checklist

Before running tests, verify in serial monitor:

- [ ] Device shows "✓ WiFi connected"
- [ ] Device shows "✓ MQTT connected"
- [ ] Device shows "✓ Radio listening for packets"
- [ ] Press `s` - statistics appear
- [ ] Press `c` then `5` - configuration displays
- [ ] Note your Node ID and Topic Prefix

**If any fail:**
```
Press 'c' to enter config menu
Configure WiFi (option 1)
Configure MQTT (option 2)
Save (option 6)
Restart (option 8)
```

## 📊 Test Output Example

```
========================================
MQTT BACKHAUL TEST
========================================
Serial Port: /dev/cu.usbserial-0001
MQTT Broker: mqtt.ripplenetworks.com.au:8883
Topic Prefix: MESHCORE/AU/NSW
========================================

📡 LoRa RX detected (line 145): 32 bytes, RSSI: -85 dBm, SNR: 8.5 dB
   Text: "Hello World"
📨 MQTT /raw: 32 bytes, RSSI: -85, gateway: MQTT-Gateway
📨 MQTT /messages: from=305419896, msg='Hello World'
✓ MATCHED: LoRa line 145 → MQTT MESHCORE/AU/NSW/messages
  Latency: 125ms, Reason: text content match

========================================
TEST RESULTS
========================================

LoRa Packets:
  RX: 5
  TX: 3

MQTT Messages Received:
  /raw: 5          ✓
  /messages: 4     ✓
  /adverts: 2      ✓
  Total: 11

Matching:
  Matched packets: 5
  Test packets sent: 3

✓ ALL TESTS PASSED
```

## 🚨 Troubleshooting

### Serial Port Not Found

```bash
# macOS
ls -l /dev/cu.usbserial-*

# Linux - check permissions
sudo usermod -a -G dialout $USER
# Logout and login again
```

### Python Modules Missing

```bash
pip install paho-mqtt pyserial

# Or use virtual environment
python3 -m venv venv
source venv/bin/activate
pip install paho-mqtt pyserial
```

### Gateway Not Responding

1. Unplug and replug USB cable
2. Check device has power (LED on)
3. Verify with serial monitor: `screen /dev/cu.usbserial-XXXXX 115200`
4. Press `r` to restart gateway

### MQTT Connection Issues

1. Check WiFi is connected (serial output)
2. Verify MQTT broker reachable: `ping mqtt.ripplenetworks.com.au`
3. Test MQTT directly:
   ```bash
   mosquitto_sub -h mqtt.ripplenetworks.com.au -p 8883 \
     -u nswmesh -P nswmesh -t "#" --insecure
   ```
4. Check firewall/network restrictions

### No LoRa Traffic

1. Verify antenna is attached
2. Check LoRa frequency matches network
3. Transmit from another device
4. Review LoRa config: Press `c` then `3`

## 📝 After Testing

### If Tests Pass

1. ✅ Gateway is functioning correctly
2. All three features verified
3. Document configuration for reference

### If MQTT Backhaul Test Fails (Only ADVERTs)

1. **Save test output** to file
2. **Check configuration**:
   ```
   Press 'c' → '2' (MQTT Settings)
   Verify: publishRaw = yes
   Verify: publishDecoded = yes
   ```
3. **Review code** in `src/main.cpp` lines 530-561
4. **Check MQTT logs**:
   ```bash
   ssh root@mqtt.ripplenetworks.com.au
   tail -f /var/log/mosquitto/mosquitto.log
   ```
5. **Create issue** with test results

### Test Reports

Find detailed results in:
```
tools/test_report_YYYYMMDD_HHMMSS.json
```

## 💡 Tips

- **Start with the quick test**: `./quick_test.sh`
- **Run MQTT Backhaul first**: It's the most critical
- **Keep serial monitor open**: Watch for errors
- **Subscribe to MQTT**: See messages in real-time
- **Test with traffic**: Have another LoRa device send messages

## 🔗 MQTT Topic Structure

Your gateway publishes to:

```
MESHCORE/AU/NSW/raw          → All LoRa packets (hex)
MESHCORE/AU/NSW/messages     → Decoded text messages
MESHCORE/AU/NSW/adverts      → Node advertisements
MESHCORE/AU/NSW/gateway/{id}/status → Gateway status
MESHCORE/AU/NSW/gateway/{id}/stats  → Gateway statistics
```

Subscribe to all:
```bash
mosquitto_sub -h mqtt.ripplenetworks.com.au -p 8883 \
  -u nswmesh -P nswmesh \
  -t "MESHCORE/AU/NSW/#" --insecure -v
```

## 📞 Support

- **GitHub Issues**: Report bugs with test outputs
- **MeshCore Discord**: Community support
- **Serial Logs**: Always capture full output
- **MQTT Logs**: SSH access available for debugging

---

## Next Steps

1. **Run** `cd tools && ./quick_test.sh`
2. **Start with** MQTT Backhaul test (option 2)
3. **Review** test output carefully
4. **Check** for "Only ADVERTs on MQTT" warning
5. **Document** any issues found
6. **Report** bugs with test results attached

**Ready? → `cd tools && ./quick_test.sh`**

