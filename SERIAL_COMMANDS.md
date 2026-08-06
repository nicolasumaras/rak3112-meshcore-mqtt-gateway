# Serial Commands Reference

Quick reference for all serial commands and configuration options.

## Runtime Commands

These commands work anytime (press the key in serial monitor). By default, when you connect over serial you will see live radio/repeater/MQTT activity scrolling. Press `c` to pause live activity and enter the menu.

| Key | Command | Description |
|-----|---------|-------------|
| `c` | Configuration Menu | Enter interactive configuration mode (pauses live view) |
| `s` | Show Statistics | Display packet counts, uptime, memory |
| `r` | Restart | Reboot the device |

## Configuration Menu

Press `c` to enter configuration menu (the menu does NOT open automatically), then use these options:

### Main Menu Options

| # | Menu Item | Description |
|---|-----------|-------------|
| 1 | WiFi Settings | Configure WiFi connection |
| 2 | MQTT Settings | Configure MQTT broker and topics |
| 3 | LoRa Settings | Configure radio parameters |
| 4 | Repeater Settings | Configure mesh network behavior |
| 5 | Show Configuration | Display all current settings |
| 6 | Save Configuration | Write settings to flash memory |
| 7 | Reset to Defaults | Restore factory default settings |
| 8 | Restart Device | Reboot with new settings |
| 0 | Exit Configuration | Return to normal operation |

## Configuration Options Detail

### 1. WiFi Settings

```
Enable WiFi (y/n): y
WiFi SSID: MyNetwork
WiFi Password: ********
```

**Options:**
- `Enable`: Enable or disable WiFi connectivity
- `SSID`: Network name (max 64 characters)
- `Password`: Network password (max 64 characters)

**Notes:**
- ESP32 only supports 2.4GHz WiFi
- WPA2 Personal/Enterprise supported
- Hidden SSIDs supported

---

### 2. MQTT Settings (ISO-coded topics)

```
Enable MQTT (y/n): y
MQTT Server: mqtt.example.com
MQTT Port: 1883
MQTT Username: myuser
MQTT Password: ********
Client ID: (auto from Repeater Node Name)
Base Prefix (e.g. MESHCORE): MESHCORE
Country ISO2 (optional): AU
Region ISO (optional): NSW
Effective Topic Prefix: MESHCORE/AU/NSW
Publish raw packets (y/n): y
Publish decoded messages (y/n): y
Subscribe to commands (y/n): y
```

**Options:**
- `Enable`: Enable or disable MQTT bridging
- `Server`: Broker hostname or IP (max 128 chars)
- `Port`: Broker port (usually 1883 or 8883)
- `Username`: MQTT authentication username (optional)
- `Password`: MQTT authentication password (optional)
- `Client ID`: Auto-derived from Repeater Node Name
- `Base Prefix`: Root of topic hierarchy (default `MESHCORE`)
- `Country ISO2`: Optional ISO country code (e.g., `AU`, `US`, `NZ`)
- `Region ISO`: Optional ISO-3166-2 subdivision code part (e.g., `NSW`, `CA`, `AUK`)
- `Effective Topic Prefix`: Uppercase hierarchical prefix computed from above
- `Publish Raw`: Send raw hex packet data
- `Publish Decoded`: Send parsed message data
- `Subscribe Commands`: Accept commands from MQTT

**Port Numbers:**
- `1883` - Standard MQTT (unencrypted)
- `8883` - MQTT over TLS/SSL
- `1884` - MQTT WebSocket (if supported by broker)

**Notes:**
- Empty username = anonymous connection
- Client ID must be unique per broker
- Effective topic prefix examples (normalized, uppercase):
  - `MESHCORE`
  - `MESHCORE/AU`
  - `MESHCORE/AU/NSW`
  - `MESHCORE/NZ/AUK`
  - When Region is empty, the gateway also subscribes to all sub-regions for raw/messages and commands.

---

### 3. LoRa Settings

```
Frequency (MHz): 915.0
Bandwidth (kHz): 125.0
Spreading Factor (7-12): 7
Coding Rate (5-8): 5
TX Power (2-20 dBm): 20
Sync Word (hex): 0x12
Enable CRC (y/n): y
```

**Frequency by Region:**
- `433.0` - Asia, some EU regions
- `868.0` - Europe (863-870 MHz)
- `915.0` - North America (902-928 MHz)
- `923.0` - Asia-Pacific

**Bandwidth Options:**
- `7.8` - Very narrow, longest range, slowest
- `10.4`
- `15.6`
- `20.8`
- `31.25`
- `41.7`
- `62.5`
- `125.0` - Standard, good balance
- `250.0` - Wide, faster but shorter range
- `500.0` - Very wide, fastest but shortest range

**Spreading Factor:**
- `7` - Fastest, shortest range (~2km)
- `8` - Fast, short range (~4km)
- `9` - Moderate speed/range (~6km)
- `10` - Slower, longer range (~8km)
- `11` - Slow, long range (~11km)
- `12` - Slowest, maximum range (~15km)

**Coding Rate:**
- `5` - 4/5 - Least overhead, fastest
- `6` - 4/6 - Low overhead
- `7` - 4/7 - Medium overhead
- `8` - 4/8 - Most overhead, best error correction

**TX Power:**
- `2` - Minimum (for testing nearby)
- `10` - Low power
- `17` - Standard
- `20` - Maximum (regulatory limit in most regions)

**Sync Word:**
- `0x12` - Default private network
- `0x34` - LoRaWAN public network
- `0xXX` - Custom (must match all nodes)
- Input accepts hex in `0xNN` or `NN` form (case-insensitive); non-hex chars ignored.

**Notes:**
- ⚠️ Restart required after changing LoRa settings
- All mesh nodes must use identical LoRa parameters
- Higher SF = longer range but slower speed
- Check local regulations for frequency and power limits

---

### 4. Repeater Settings

```
Node Name: MQTT-Gateway
Max Hops (1-7): 3
Auto ACK (y/n): y
Broadcast Enabled (y/n): y
Route Timeout (seconds): 300
```

**Options:**
- `Node Name`: Friendly name (max 32 characters)
- `Max Hops`: Maximum packet forwarding hops
- `Auto ACK`: Automatically acknowledge packets
- `Broadcast`: Allow broadcasting to all nodes
- `Route Timeout`: Seconds before route expires

**Max Hops Recommendations:**
- `1` - No repeating (node only)
- `2` - Small networks (< 10 nodes)
- `3` - Medium networks (10-50 nodes) ← Recommended
- `4` - Large networks (50-100 nodes)
- `5+` - Very large networks (use with caution)

**Route Timeout:**
- `60` - 1 minute (fast-moving nodes)
- `300` - 5 minutes (default)
- `600` - 10 minutes (stationary nodes)
- `1800` - 30 minutes (very stable network)

**Notes:**
- Node ID is auto-generated from chip MAC
- Lower max hops = less network congestion
- Higher route timeout = fewer route discoveries

---

## Configuration Examples

### Home Gateway (Stationary)
```
WiFi: Enabled
MQTT: Enabled
LoRa: 915MHz, SF7, 125kHz, 20dBm
Repeater: Max Hops 3, Auto ACK
```

### Portable Gateway (Battery)
```
WiFi: Disabled (or mobile hotspot)
MQTT: Enabled (when WiFi available)
LoRa: 915MHz, SF9, 125kHz, 17dBm
Repeater: Max Hops 2, Auto ACK
```

### High-Range Gateway
```
WiFi: Enabled
MQTT: Enabled
LoRa: 915MHz, SF12, 62.5kHz, 20dBm
Repeater: Max Hops 4, Auto ACK
```

### Urban Dense Network
```
WiFi: Enabled
MQTT: Enabled
LoRa: 915MHz, SF7, 250kHz, 17dBm
Repeater: Max Hops 2, Auto ACK
```

## Tips

### Entering Configuration
1. Open serial monitor at **115200 baud**
2. Watch live activity (default)
3. Press `c` key to enter the menu (pauses live view)
4. Wait for menu to appear, then enter a number + Enter

### Saving Settings
- ⚠️ Always select option `6` to save before restarting!
- Settings are stored in ESP32 NVS (non-volatile storage)
- Survives power cycles and reboots
- Can be erased with option `7` (Reset to Defaults)

### Troubleshooting Configuration
If settings won't save:
1. Try `7` to reset to defaults
2. Configure again
3. Save with option `6`
4. Restart with option `8`

### Testing Changes
After configuring:
1. Save configuration (`6`)
2. Restart device (`8`)
3. Check serial output for connection status
4. Use `s` command to verify operation

## Default Values

If you reset to defaults (option `7`), these values are restored:

| Setting | Default Value |
|---------|---------------|
| **WiFi** |
| SSID | (empty) |
| Password | (empty) |
| Enabled | No |
| **MQTT** |
| Server | mqtt.example.com |
| Port | 1883 |
| Username | (empty) |
| Password | (empty) |
| Client ID | (auto from Node Name) |
| Base Prefix | meshcore |
| Country | (empty) |
| Region | (empty) |
| Effective Topic Prefix | meshcore |
| Enabled | No |
| **LoRa** |
| Frequency | 915.0 MHz |
| Bandwidth | 125.0 kHz |
| Spreading Factor | 7 |
| Coding Rate | 5 |
| TX Power | 20 dBm |
| Sync Word | 0x12 |
| CRC | Enabled |
| **Repeater** |
| Node Name | MQTT-Gateway |
| Max Hops | 3 |
| Auto ACK | Yes |
| Broadcast | Yes |
| Route Timeout | 300 seconds |

---

## Serial Monitor Setup

### PlatformIO (VS Code)
1. Click "PlatformIO" icon in sidebar
2. Click "Serial Monitor" under your device
3. Baud rate is set automatically to 115200

### Arduino IDE
1. Tools → Serial Monitor
2. Set baud rate to **115200**
3. Set line ending to **Newline** or **Both NL & CR**

### External Terminal (Windows)
```powershell
# Using PuTTY
putty -serial COM3 -sercfg 115200,8,n,1,N

# Using PowerShell (Windows 10+)
$port = new-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.Open()
```

### External Terminal (Linux/Mac)
```bash
# Using screen
screen /dev/ttyUSB0 115200

# Using minicom
minicom -D /dev/ttyUSB0 -b 115200

# Using picocom
picocom /dev/ttyUSB0 -b 115200
```

---

**Need more help? Check [README.md](README.md) or [QUICKSTART.md](QUICKSTART.md)**

