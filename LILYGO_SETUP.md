# LilyGo LoRa32 V2.1_1.6 Setup Guide

Quick setup guide for your specific board.

## 🎯 Your Board Configuration

**Board:** LilyGo LoRa32 V2.1_1.6  
**COM Port:** COM7  
**Status:** ✅ Pre-configured and ready to use!

## 📋 Hardware Specifications

### LoRa Module
- **Chip:** SX1276/SX1278 (433/868/915 MHz)
- **Frequency:** Configure based on your region
  - 915 MHz (North America) - Default
  - 868 MHz (Europe)
  - 433 MHz (Asia)

### ESP32
- **RAM:** 520 KB
- **Flash:** 4 MB
- **WiFi:** 2.4 GHz 802.11 b/g/n
- **Bluetooth:** BLE 4.2

### Display
- **Type:** 0.96" OLED (128x64)
- **Driver:** SSD1306
- **I2C Address:** 0x3C
- **Pins:** SDA=21, SCL=22, RST=16

### Pin Configuration (Already Set)
```
LoRa Radio:
  SCK:  GPIO 5
  MISO: GPIO 19
  MOSI: GPIO 27
  CS:   GPIO 18
  RST:  GPIO 23
  DIO0: GPIO 26
  DIO1: GPIO 33
  DIO2: GPIO 32

OLED Display (Optional):
  SDA:  GPIO 21
  SCL:  GPIO 22
  RST:  GPIO 16
```

## 🚀 Quick Start

### 1. Build and Upload

The project is already configured for your board on COM7. Just click the upload button in PlatformIO or run:

```bash
pio run --target upload
```

### 2. Open Serial Monitor

```bash
pio device monitor
```

Or click the Serial Monitor button in PlatformIO.

### 3. Configure Settings

Press `c` to enter the configuration menu and set:

**WiFi Settings:**
```
SSID: YourWiFiName
Password: YourWiFiPassword
```

**MQTT Settings:**
```
Server: mqtt.yourserver.com
Port: 1883
```

**LoRa Settings (US):**
```
Frequency: 915.0 MHz
Bandwidth: 125.0 kHz
Spreading Factor: 7
TX Power: 20 dBm
```

**LoRa Settings (EU):**
```
Frequency: 868.0 MHz
Bandwidth: 125.0 kHz
Spreading Factor: 7
TX Power: 14 dBm (EU limit)
```

### 4. Save and Restart

- Press `6` to save configuration
- Press `8` to restart

## 🔧 Troubleshooting

### Upload Issues

**Problem:** Can't upload firmware

**Solutions:**
1. Press and hold the **BOOT** button during upload
2. Verify COM7 is the correct port:
   ```bash
   pio device list
   ```
3. Try a different USB cable (data cable, not charge-only)
4. Reduce upload speed in platformio.ini:
   ```ini
   upload_speed = 115200
   ```

### LoRa Not Working

**Problem:** No LoRa packets received

**Solutions:**
1. Check antenna is connected (REQUIRED - never transmit without antenna!)
2. Verify frequency matches your region and other nodes
3. Ensure you're using the correct frequency band:
   - V2.1_1.6 typically supports 433/868/915 MHz
   - Check the label on your board
4. Start with SF7 and 125kHz bandwidth for testing
5. Make sure sync word (0x12) matches other MeshCore nodes

### WiFi Not Connecting

**Problem:** WiFi connection fails

**Solutions:**
1. Ensure SSID and password are correct
2. Must be 2.4 GHz network (ESP32 doesn't support 5 GHz)
3. Try moving closer to router
4. Check if MAC address filtering is enabled on router
5. Some enterprise WiFi networks may not work

### MQTT Not Connecting

**Problem:** Can't connect to MQTT broker

**Solutions:**
1. Verify broker address is correct (IP or hostname)
2. Check port (usually 1883 for non-TLS)
3. Test broker from computer:
   ```bash
   mosquitto_pub -h mqtt.yourserver.com -t test -m "test"
   ```
4. Check if authentication is required
5. Verify firewall allows port 1883

## 📊 Testing Your Setup

### 1. Basic Functionality Test

After uploading, you should see:
```
╔════════════════════════════════════════════════════════╗
║        MeshCore MQTT Gateway v1.0.0                    ║
╚════════════════════════════════════════════════════════╝

✓ Configuration loaded
✓ Generated Node ID: 0xXXXXXXXX
✓ LoRa initialized
✓ WiFi connected
✓ MQTT initialized
✓ Gateway started successfully!
```

### 2. Send Test Command

Press `s` to see statistics:
```
┌── Gateway Statistics ──────────────────────────────────┐
│ Uptime:           123                                  │
│ Packets Received: 0                                    │
│ Packets Sent:     0                                    │
│ WiFi RSSI:        -65                                  │
│ MQTT:             Connected                            │
└────────────────────────────────────────────────────────┘
```

### 3. Test MQTT Connection

From another computer, subscribe to messages:
```bash
mosquitto_sub -h mqtt.yourserver.com -t "MESHCORE/#" -v
```

You should see the gateway status message.

### 4. Test LoRa (with another MeshCore node)

If you have another MeshCore device nearby, send a message and watch it appear in the serial monitor and MQTT.

## 🎨 Optional: OLED Display Support

Your board has an OLED display! To use it:

### Add Display Library

Update `platformio.ini`:
```ini
lib_deps = 
    https://github.com/meshcore-dev/MeshCore.git
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.3
    thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays@^4.4.0
```

### Example Display Code

Add to `src/main.cpp`:
```cpp
#include <SSD1306Wire.h>

SSD1306Wire display(0x3c, OLED_SDA, OLED_SCL, GEOMETRY_128_64, I2C_ONE, 800000);

void setupDisplay() {
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    display.clear();
    display.drawString(0, 0, "MeshCore MQTT");
    display.drawString(0, 12, "Gateway v1.0");
    display.display();
}

void updateDisplay() {
    display.clear();
    display.drawString(0, 0, "MeshCore Gateway");
    display.drawString(0, 12, String("RX: ") + packetsReceived);
    display.drawString(0, 24, String("TX: ") + packetsSent);
    
    if (WiFi.status() == WL_CONNECTED) {
        display.drawString(0, 36, "WiFi: OK");
    } else {
        display.drawString(0, 36, "WiFi: --");
    }
    
    if (mqttHandler && mqttHandler->isConnected()) {
        display.drawString(0, 48, "MQTT: OK");
    } else {
        display.drawString(0, 48, "MQTT: --");
    }
    
    display.display();
}
```

## 🔋 Power Options

### USB Power
- 5V via USB-C port
- Most common for development and testing

### Battery Power
- JST 2.0mm 2-pin connector
- 3.7V LiPo battery (500-2000 mAh recommended)
- Built-in charging circuit
- Charges while connected to USB

### Solar Power
- Can use solar panel with battery
- Panel: 5-6V, 500mA or higher
- Connect through JST connector or solder pads

## 📏 Board Dimensions

- **Length:** ~51mm
- **Width:** ~26mm
- **Height:** ~12mm (with headers)
- **Weight:** ~10g

## 🌐 Useful Resources

- [LilyGo Official GitHub](https://github.com/LilyGO/TTGO-LORA32)
- [LilyGo Wiki](http://www.lilygo.cn/prod_view.aspx?TypeId=50060&Id=1326)
- [MeshCore Discord](https://discord.gg/meshcore)
- [PlatformIO Boards](https://docs.platformio.org/en/latest/boards/espressif32/ttgo-lora32-v21.html)

## 🎯 Next Steps

1. ✅ Upload firmware (you're probably done with this!)
2. ⬜ Configure via serial (press `c`)
3. ⬜ Test MQTT connection
4. ⬜ Test with other MeshCore nodes
5. ⬜ Deploy and enjoy!

## 💡 Tips

- **Always connect antenna before powering on** - Transmitting without antenna can damage the LoRa module
- **Keep TX power at or below 17 dBm** when powered by battery to avoid brownouts
- **Use shielded enclosure** if deploying outdoors to protect from weather
- **Monitor battery voltage** if using battery power - add voltage monitoring code
- **Flash LED on activity** for visual feedback without display

## 🆘 Need Help?

- Check [SERIAL_COMMANDS.md](SERIAL_COMMANDS.md) for command reference
- MeshCore integration is already complete in this firmware
- Visit [MeshCore Discord](https://discord.gg/meshcore) for community support
- Open an issue on GitHub for bugs

---

**Your gateway is ready to go! Happy meshing! 📡✨**

