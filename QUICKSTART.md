# Quick Start Guide

Get your MeshCore MQTT Gateway up and running in 10 minutes!

## 📦 What You Need

- ESP32 LoRa board (Heltec V3, TTGO, or generic ESP32 + SX127x)
- USB cable
- Computer with VS Code + PlatformIO
- WiFi network
- MQTT broker (local or cloud)

## 🚀 5-Step Setup

### 1️⃣ Clone and Open
```bash
git clone <repo-url>
cd MeshCore-MQTT-Gateway
code .  # Opens in VS Code
```

### 2️⃣ Select Your Board
Edit `platformio.ini`:
```ini
[platformio]
default_envs = esp32_mqtt_gateway  # or heltec_v3_mqtt
```

### 3️⃣ Build and Upload
Click the PlatformIO upload button (→) or:
```bash
pio run --target upload
```

### 4️⃣ Configure via Serial
1. Open Serial Monitor (115200 baud)
2. Press `c` to enter config menu
3. Configure WiFi:
   ```
   1 → WiFi Settings
   Enable: y
   SSID: YourWiFiName
   Password: YourWiFiPassword
   ```
4. Configure MQTT:
   ```
   2 → MQTT Settings
   Enable: y
   Server: mqtt.example.com
   Port: 1883 (use 8883 for TLS)
   [Rest can use defaults]
   ```
   Note: Country/Region inputs are normalized to uppercase and spaces removed. Use ISO2 for country (e.g., AU) and state (e.g. NSW). For TLS:
   - Set Port to 8883 and Enable TLS = y
   - Use custom CA = n (firmware embeds the broker CA)
   - The firmware will retry via resolved IP if the certificate CN is an IP.
5. Save and restart:
   ```
   6 → Save Configuration
   8 → Restart Device
   ```

### 5️⃣ Test It!
Subscribe to MQTT to see messages:
```bash
mosquitto_sub -h mqtt.example.com -t "MESHCORE/#" -v
```

## 🎉 You're Done!

The gateway is now:
- ✅ Repeating LoRa mesh messages
- ✅ Publishing messages to MQTT
- ✅ Forwarding MQTT commands to LoRa

## 📱 Next Steps

### Test with Python
```bash
cd examples
pip install paho-mqtt
python mqtt_test_client.py --broker mqtt.example.com
```

### Monitor in Real-time
```bash
# See all messages
mosquitto_sub -h mqtt.example.com -t "MESHCORE/messages"

# See gateway stats
mosquitto_sub -h mqtt.example.com -t "MESHCORE/gateway/+/stats"
```

### See Adverts
```bash
mosquitto_sub -h mqtt.example.com -t "MESHCORE/adverts" -v
```

### Send a Test Message
```bash
mosquitto_pub -h mqtt.example.com \
  -t "MESHCORE/commands/send" \
  -m '{"to": 4294967295, "message": "Hello MeshCore!"}'
```

## 🔧 Common Issues

**WiFi won't connect?**
- Check SSID/password spelling
- Make sure it's 2.4GHz WiFi (not 5GHz)
- Move closer to router

**MQTT won't connect?**
- Verify broker address and port
- Check if authentication is required
- Test with: `mosquitto_pub -h mqtt.example.com -t test -m test`

**No LoRa packets?**
- Check antenna is connected
- Verify frequency matches your region (915MHz US, 868MHz EU)
- Make sure other MeshCore nodes are nearby

**Can't upload firmware?**
- Press and hold BOOT button during upload
- Try a different USB cable
- Check correct COM port is selected

## 📚 Learn More

- **Full README**: Detailed documentation → [README.md](README.md)
- **Examples**: MQTT clients and integrations → [examples/](examples/)
- **MeshCore Docs**: <https://github.com/meshcore-dev/MeshCore>

## 🆘 Get Help

- MeshCore Discord: <https://discord.gg/meshcore>
- GitHub Issues: Report bugs and request features
- MeshCore Community: Friendly and helpful!

---

**Happy Meshing! 📡**

