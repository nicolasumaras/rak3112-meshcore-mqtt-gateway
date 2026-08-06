# MeshCore MQTT Gateway - THIS IS BETA SOFTWARE and still under development. Has a bug with radio starting. Fixing it.

A powerful gateway firmware that bridges MeshCore LoRa mesh networks with MQTT brokers, enabling bidirectional message flow between LoRa devices and MQTT-based applications.

## 🌟 Features

- **Full MeshCore Repeater Functionality** - Acts as a mesh network repeater, extending network range
- **MQTT Bridging** - Forward LoRa mesh messages to MQTT broker and vice versa
- **Serial Configuration** - Easy-to-use serial menu for configuring all settings
- **WiFi Connectivity** - Connect to your local network
- **Persistent Configuration** - Settings stored in ESP32 NVS (non-volatile storage)
- **Real-time Statistics** - Monitor packets, uptime, and connection status
- **Multiple Publishing Modes** - Publish raw hex packets and/or decoded messages
- **Remote Commands** - Send commands from MQTT to the gateway
- **Gateway Status Reporting** - Automatic online/offline status with last will
- **NTP Clock Sync** - Automatic time sync (NTP) with timezone support; required for TLS certificate validation

## 📚 Documentation Index

- **Getting started**
  - [FLASH_NOW.md](FLASH_NOW.md) — Flash the LilyGo LoRa32 V2.1 firmware immediately
  - [QUICKSTART.md](QUICKSTART.md) — 5-step setup to build, upload, and configure
  - [LILYGO_SETUP.md](LILYGO_SETUP.md) — Board-specific setup and troubleshooting

- **Configuration and operation**
  - [SERIAL_COMMANDS.md](SERIAL_COMMANDS.md) — Runtime keys and full menu reference
  - Tools and helpers:
    - [tools/mqtt_tls_check.py](tools/mqtt_tls_check.py) — Verify MQTT over TLS end-to-end
    - [tools/upload_ca_serial.py](tools/upload_ca_serial.py) — Upload a custom TLS CA over serial
    - [tools/configure_mqtt_topic_selector.py](tools/configure_mqtt_topic_selector.py) — Pick ISO-coded MQTT topic prefixes

- **Development and integration**
  - MeshCore integration is already complete in this firmware.
  

- **Reference**
  - [platformio.ini](platformio.ini) — Build environments and dependencies
  - [src/](src/) — Firmware sources (`main.cpp`, `config.h`, `mqtt_handler.h`, etc.)
  - [tools/](tools/) — Utility scripts for configuration, testing, and diagnostics

## 📋 Requirements

### Hardware
- ESP32-based LoRa board (Heltec v3, generic ESP32+SX127x, etc.)
- LoRa radio module (SX1276/SX1278 or compatible)
- USB cable for programming and serial configuration

### Software
- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- [MeshCore Library](https://github.com/meshcore-dev/MeshCore)

## 🚀 Quick Start

### 1. Clone and Open Project

```bash
git clone <your-repo-url>
cd <repo-dir>
```

Open in VS Code with PlatformIO extension installed.

### 2. Configure Hardware

Edit `platformio.ini` to select your target board:

```ini
[platformio]
default_envs = lilygo_lora32_v21  ; or esp32_mqtt_gateway, heltec_v3_mqtt
```

If using a custom board, adjust pin definitions in `src/config.h`:

```cpp
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
// ... etc
```

### 3. Build and Upload

```bash
pio run --target upload
```

Or use the PlatformIO upload button in VS Code.

### 4. Initial Configuration

1. Open serial monitor (115200 baud)
2. Observe live LoRa/MQTT activity in real time (default view)
3. Press `c` to enter configuration menu (the menu does NOT open automatically)
4. Configure WiFi settings (option 1)
5. Configure MQTT settings (option 2)
6. Configure LoRa settings (option 3)
7. Configure Repeater settings (option 4)
8. Configure Clock / NTP (option 13) — recommended for TLS
9. Save configuration (option 6)
10. Restart device (option 8)

## ⚙️ Configuration

### Serial Configuration Menu

The gateway shows live radio/repeater/MQTT activity by default when you connect. Press `c` to pause live activity and enter the interactive configuration menu.

**Main Menu Options:**
- `1` - WiFi Settings (SSID, password, enable/disable)
- `2` - MQTT Settings (broker, port, credentials, topics; ISO-coded prefix)
- `3` - LoRa Settings (frequency, bandwidth, spreading factor, etc.)
- `4` - Repeater Settings (node name, max hops, timeouts)
- `5` - Show Current Configuration
- `6` - Save Configuration
- `7` - Reset to Defaults
- `8` - Restart Device
- `0` - Exit Configuration

**Runtime Commands:**
- `c` - Enter configuration menu (pauses live activity until you exit)
- `s` - Show statistics
- `r` - Restart device

### Configuration Structure

#### WiFi Configuration
```
SSID: your-wifi-ssid
Password: your-wifi-password
Enabled: yes
```

#### MQTT Configuration (ISO-coded topics)
```
Server: mqtt.example.com (or IP address)
Port: 1883 (or 8883 for TLS)
Username: (optional)
Password: (optional)
Client ID: (auto from Repeater Node Name)
Base Prefix: MESHCORE
Country: ISO2 code (optional, e.g. AU, NZ, US)
Region: ISO-3166-2 subdivision code part (optional, e.g. NSW, AUK, CA)
Effective Topic Prefix: uppercase hierarchical, e.g. MESHCORE/AU/NSW
Publish Raw: yes
Publish Decoded: yes
Subscribe Commands: yes
TLS: on/off (8883 when on)
Custom CA: on/off
```

#### LoRa Configuration
```
Frequency: 915.0 MHz (US) or 868.0 MHz (EU) or 433.0 MHz or 915.8 Mhz (Australia)
Bandwidth: 250 kHz
Spreading Factor: 7-12 (7=fastest, 12=longest range)
Coding Rate: 5-8
TX Power: 2-20 dBm
Sync Word: 0x34
Enable CRC: yes
```

#### Repeater Configuration
```
Node Name: MQTT-Gateway
Node ID: 0x12345678 (auto-generated from chip ID)
Max Hops: 3
Auto ACK: yes
Broadcast Enabled: yes
Route Timeout: 300 seconds
```

#### Clock / NTP Configuration
```
NTP Server: pool.ntp.org
Timezone offset (minutes): 0
Auto-sync at boot: yes
```

Notes:
- Accurate time is required for TLS certificate validation. When WiFi is enabled and Auto-sync is on, the firmware will sync time at boot. You can also trigger a manual sync from the serial menu (Clock Sync, option 13).

## 📡 MQTT Topics
## 🔒 Using a Custom TLS CA

If you run your own MQTT server with a private CA, you can upload the CA PEM so the gateway validates the broker over TLS.

### Option A: Serial Menu (interactive)
1) Open serial (115200) and press `c` to enter the menu.
2) Choose `2` (MQTT Settings).
3) Set: `Server`, `Port` (use 8883), `Enable TLS` = y, credentials as needed.
4) When prompted:
   - `Use custom CA (y/n)`: type `y`.
   - Paste the full PEM, including `-----BEGIN CERTIFICATE-----` and `-----END CERTIFICATE-----`.
   - On a new line, type `ENDCA` and press Enter.
5) Save configuration (option `6`).
6) Restart device (option `8`).

Notes:
- Paste only the CA (issuer) certificate used to sign your broker's server certificate.
- Maximum PEM size: ~2 KB.
- To revert to the built-in CA, set `Use custom CA` to `n` and save.

### Option B: Scripted upload
Use the helper script to automate pasting the PEM over serial:

```bash
python3 tools/upload_ca_serial.py /dev/cu.usbserial-XXXXX /path/to/ca.pem
```

This keeps existing MQTT settings and only turns on Custom CA and uploads the PEM.

### mqtt.ripplenetworks.com.au (TLS 8883) quick steps

1) MQTT Settings:
   - Server: mqtt.ripplenetworks.com.au
   - Port: 8883
   - Enable TLS: y
   - Username: nswmesh
   - Password: nswmesh
2) TLS CA:
   - Use custom CA: n (uses built-in firmware CA)
   - No CA paste or upload required.
3) Save (6) and Restart (8).

Notes:
- If the DNS was previously proxied via Cloudflare, ensure proxy is disabled so the origin broker certificate is presented.
- Prefer setting the server by hostname; if the certificate CN is bound to the broker IP, the firmware now automatically retries using the resolved IP to complete TLS.


### Verify MQTT over TLS (LilyGo LoRa32 V2.1)

1) Build and upload
```bash
pio run -e lilygo_lora32_v21 --target upload
```

2) Open serial monitor
```bash
pio device monitor -p /dev/cu.usbserial-XXXXX -b 115200
```

3) Configure via menu (`c` → `2` MQTT Settings)
- Enable MQTT: y
- Server: your broker hostname (e.g., mqtt.ripplenetworks.com.au)
- Port: 8883
- Enable TLS: y
- Use custom CA: n (uses built-in firmware CA)
- Review the printed “Effective Topic Prefix”
- Save (6) and Restart (8)

4) Confirm in serial output
- ✓ WiFi connected
- Setting time via NTP for TLS… ✓ Time set
- ✓ MQTT connected

5) Validate from your computer
```bash
# With broker CA file (preferred)
mosquitto_sub -h <broker> -p 8883 -u <user> -P <pass> \
  -t "<prefix>/#" --cafile /path/to/ca.pem -v

# If you don't have the CA locally (temporary test only)
mosquitto_sub -h <broker> -p 8883 -u <user> -P <pass> \
  -t "<prefix>/#" --insecure -v
```

Or use the helper script to perform a quick end-to-end TLS check and publish a test message:

```bash
python3 tools/mqtt_tls_check.py \
  --host mqtt.ripplenetworks.com.au --port 8883 \
  --username nswmesh --password nswmesh \
  --prefix <effective-prefix> \
  --cafile mqtt_ca.pem
```


### Published Topics

#### Raw Packets
Topic: `{prefix}/raw`  where `{prefix}` can be `MESHCORE`, `MESHCORE/AU`, or `MESHCORE/AU/NSW`

```json
{
  "timestamp": 12345678,
  "rssi": -85,
  "snr": 8.5,
  "data": "0102030405...",
  "length": 32
}
```

#### Decoded Messages
Topic: `{prefix}/messages`

```json
{
  "timestamp": 12345678,
  "from": 305419896,
  "to": 305419897,
  "message": "Hello from LoRa!",
  "type": 1,
  "rssi": -85,
  "snr": 8.5,
  "hops": 2,
  "gateway": "meshcore_gateway_001"
}
```

#### Adverts
Topic: `{prefix}/adverts`

```json
{
  "timestamp": 12345678,
  "nodeId": 305419896,
  "name": "MQTT-Gateway",
  "lat": -33.86,
  "lon": 151.21,
  "gateway": "meshcore_gateway_001"
}
```

Notes:
- Includes adverts received over LoRa from other nodes, not only the gateway’s own periodic advert. Each advert JSON shows the originating `nodeId` and the publishing `gateway`.
- When configured at a parent prefix (e.g., `MESHCORE/AU` with no region), the gateway receives child-region adverts (e.g., `MESHCORE/AU/NSW/adverts`) via hierarchical subscriptions when bridging is enabled.

#### Node Information
Topic: `{prefix}/nodes/{nodeId}`

```json
{
  "nodeId": 305419896,
  "name": "Node-001",
  "online": true,
  "timestamp": 12345678,
  "gateway": "meshcore_gateway_001"
}
```

#### Gateway Statistics
Topic: `{prefix}/gateway/{clientId}/stats`

```json
{
  "timestamp": 12345678,
  "uptime": 3600,
  "packetsReceived": 1250,
  "packetsSent": 45,
  "packetsForwarded": 1180,
  "packetsFailed": 5,
  "rssi": -65,
  "freeHeap": 156000
}
```

#### Gateway Status (Retained)
Topic: `{prefix}/gateway/{clientId}/status`

```json
{
  "online": true,
  "timestamp": 12345678,
  "ip": "192.168.1.100",
  "rssi": -65
}
```

### Subscribed Topics

#### Send Command
Topic: `{prefix}/commands/send`

Payload: Raw bytes or JSON message to send via LoRa

#### Restart Command
Topic: `{prefix}/commands/restart`

### Hierarchical Topic Prefix (ISO-based)

- Set `Base Prefix` (e.g., `MESHCORE`).
- Optionally set `Country` as ISO2 (e.g., `AU`, `US`, `NZ`).
- Optionally set `Region` as the ISO-3166-2 subdivision code part (e.g., `NSW`, `CA`, `AUK`).
- The gateway computes the effective `{prefix}` as uppercased segments without spaces.
  - Examples: `MESHCORE`, `MESHCORE/AU`, `MESHCORE/AU/NSW`, `MESHCORE/NZ/AUK`.

Note: Country and Region inputs entered via the serial menu are normalized to uppercase and any spaces are removed. If a custom country is provided, it should be ISO2. For testing, insecure TLS (skip certificate validation) can be enabled from the serial menu when TLS is on.

Wildcard subscriptions:
- If Region is empty, the gateway also subscribes to sub-regions under the selected prefix:
  - `{prefix}/+/raw`, `{prefix}/+/messages`, and `{prefix}/+/adverts`
  - Example: with `MESHCORE/AU`, the gateway receives `MESHCORE/AU/NSW/raw` automatically.

Payload: (any) - Triggers gateway restart

#### Bridged Topics (for RF rebroadcast)

When bridging is enabled (`Bridge All` = yes), the gateway consumes and may rebroadcast over LoRa the following MQTT topics under the effective `{prefix}` and, if configured at a parent level, also from child regions:

- `{prefix}/raw` — expects JSON `{ data: hex, gateway?: string }`
- `{prefix}/messages` — expects JSON `{ message: string, gateway?: string }`
- `{prefix}/adverts` — expects JSON `{ nodeId, name, lat, lon, gateway?: string }`

Messages tagged with `gateway` matching the current gateway’s `clientId` are ignored to prevent loops. Recent-packet deduplication on the RF side further reduces echoing.

## 🔧 Integration with MeshCore

The current implementation includes placeholder comments where MeshCore integration should be added. Follow these steps to complete the integration:

### 1. Include MeshCore Headers

In `src/main.cpp`, uncomment and adjust:
```cpp
#include <MeshCore.h>
```

### 2. Initialize MeshCore

In the `setupLoRa()` function, add MeshCore initialization based on the actual library API:

```cpp
void setupLoRa() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    
    // Initialize MeshCore with your specific API calls
    MeshCore.begin();
    MeshCore.setNodeId(config.repeater.nodeId);
    MeshCore.setNodeName(config.repeater.nodeName);
    // ... etc
}
```

### 3. Handle Incoming Packets

In the `handleLoRaReceive()` function:

```cpp
void handleLoRaReceive() {
    if (MeshCore.available()) {
        uint8_t buffer[256];
        size_t length = MeshCore.receive(buffer, sizeof(buffer));
        int rssi = MeshCore.getLastRssi();
        float snr = MeshCore.getLastSnr();
        
        packetsReceived++;
        handleLoRaPacket(buffer, length, rssi, snr);
    }
}
```

### 4. Send Packets

In the `sendLoRaPacket()` function:

```cpp
void sendLoRaPacket(const uint8_t* data, size_t length) {
    if (MeshCore.send(data, length)) {
        packetsSent++;
    } else {
        packetsFailed++;
    }
}
```

## 📊 Monitoring

### Serial Monitor
- Real-time packet logging
- Connection status
- Error messages
- Statistics display

### MQTT Dashboard
Subscribe to topics to monitor:
- `{prefix}/gateway/+/status` - All gateway statuses
- `{prefix}/messages` - All mesh messages
- `{prefix}/gateway/+/stats` - Gateway statistics
 - `{prefix}/adverts` - Advert events (gateway-origin and LoRa-received)

Examples using `mosquitto_sub`:
```bash
"# Base only\n" \
mosquitto_sub -h mqtt.example.com -t "MESHCORE/#" -v

"# Country-level\n" \
mosquitto_sub -h mqtt.example.com -t "MESHCORE/AU/#" -v

# With TLS and credentials
mosquitto_sub -h mqtt.example.com -p 8883 -u user -P pass \
  -t "MESHCORE/#" --insecure -v
```

Python helper subscriber (TLS 8883):
```bash
python3 tools/mqtt_subscribe.py \
  --host mqtt.ripplenetworks.com.au --port 8883 \
  --username nswmesh --password nswmesh \
  --prefix MESHCORE/AU/NSW --insecure --timeout 15
```

### CLI: Configure ISO topics using CSC database

Use the Countries-States-Cities dataset [`dr5hn/countries-states-cities-database`](https://github.com/dr5hn/countries-states-cities-database) to select ISO codes.

1) Obtain `countries.json` and `states.json` from the repo's `json/` folder.
2) Run the topic selector:

```bash
python3 tools/configure_mqtt_topic_selector.py \
  /dev/cu.usbserial-XXXXX mqtt.example.com 8883 user pass AU NSW \
  --csc-root /path/to/countries-states-cities-database/json --base MESHCORE
```

This sets the effective prefix to `MESHCORE/AU/NSW` and restarts the device.
```

## 🛠️ Troubleshooting

### WiFi Not Connecting
- Check SSID and password in configuration
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Check signal strength
- Try restarting the gateway

### MQTT Not Connecting
- Verify broker address and port
- Check username/password if required
- Ensure broker allows connections from your network
- Check firewall rules

### TLS Connection Issues
- Ensure device shows time set via NTP before TLS connection
- Confirm broker hostname (not IP) to satisfy certificate SNI/hostname checks
- For `mosquitto_sub` tests, prefer `--cafile <issuer-ca.pem>` over `--insecure`
- If using Cloudflare or a proxy, disable proxy so the origin certificate is presented
- Verify device date/time; reboot if NTP servers were unreachable

### No LoRa Packets Received
- Verify LoRa frequency matches your region and other nodes
- Check antenna connection
- Verify spreading factor and bandwidth match mesh network
- Ensure sync word matches (default: 0x12)

### Configuration Not Saving
- Check serial output for error messages
- Try "Reset to Defaults" then reconfigure
- Ensure adequate power supply

## 🔐 Security Considerations

- **MQTT Credentials**: Store credentials securely, consider using TLS/SSL
- **Network Security**: Use VPN or secure network for MQTT traffic
- **Access Control**: Implement MQTT ACLs to restrict topic access
- **Firmware Updates**: Keep firmware updated for security patches

## 📝 Example MQTT Publishers

### Python Example

```python
import paho.mqtt.client as mqtt
import json

client = mqtt.Client("controller")
client.username_pw_set("user", "password")
client.connect("mqtt.example.com", 1883)

# Send message to LoRa mesh
message = b"\x01\x02\x03\x04"  # Your packet data
client.publish("MESHCORE/commands/send", message)

# Subscribe to messages
def on_message(client, userdata, msg):
    data = json.loads(msg.payload)
    print(f"Message from node {data['from']}: {data['message']}")

client.subscribe("MESHCORE/messages")
client.on_message = on_message
client.loop_forever()
```

### Node-RED Flow

Import this flow to visualize mesh traffic:

```json
[
    {
        "id": "mqtt-in",
        "type": "mqtt in",
        "topic": "MESHCORE/messages",
        "broker": "mqtt-broker"
    },
    {
        "id": "debug",
        "type": "debug",
        "name": "Mesh Messages"
    }
]
```

## ✅ Supported Boards (ESP focus)

- Heltec WiFi LoRa 32 V3 (`heltec_v3_mqtt`)
- Heltec WiFi LoRa 32 V2 (`heltec_v2_mqtt`)
- TTGO T-Beam SX1276 (`tbeam_sx1276`)
- LilyGo LoRa32 V2.1 1.6 (`lilygo_lora32_v21`)
- Generic ESP32 devkit + SX1276 (`esp32_mqtt_gateway`)

RAK (nRF52-based, e.g., RAK 4631) is planned; requires porting WiFi/time/MQTT stack and RadioLib pins for that platform. UF2 output will be available after porting.

## 🤝 Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see LICENSE file for details.

## 🔗 Links

- [MeshCore Project](https://github.com/meshcore-dev/MeshCore)
- [MeshCore Discord](https://discord.gg/meshcore)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [MQTT Documentation](https://mqtt.org/)

## 📮 Support

- Open an issue for bugs or feature requests
- Join MeshCore Discord for community support
- Check existing issues before creating new ones

## 🎯 Roadmap

- [x] TLS/SSL support for MQTT
- [ ] Web configuration interface
- [ ] OTA (Over-The-Air) firmware updates
- [ ] Message encryption
- [ ] Multiple MQTT broker support
- [ ] Local message logging to SD card
- [ ] REST API for local control
- [ ] Prometheus metrics endpoint

---

**Made with ❤️ for the MeshCore community**

