#!/usr/bin/env python3
"""
MeshCore MQTT Gateway Configuration Script Template

Copy this file to configure_gateway.py and fill in your settings.
The configure_gateway.py file is gitignored to protect your credentials.
"""
import serial
import time

# Configuration settings
PORT = 'COM7'  # Your COM port
BAUD = 115200

# Settings - FILL THESE IN
WIFI_SSID = "YOUR_WIFI_SSID"
WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"
MQTT_SERVER = "mqtt.example.com"
MQTT_PORT = "1883"
MQTT_USER = "your_username"
MQTT_PASS = "your_password"
LORA_FREQ = "915.0"  # 915.0 (US), 868.0 (EU), 433.0 (Asia)
LORA_BW = "125.0"    # 125.0, 250.0, or 500.0
LORA_SF = "7"        # 7-12 (higher = longer range, slower)
LORA_CR = "5"        # 5-8 (coding rate 4/5 to 4/8)
LORA_POWER = "20"    # 2-20 dBm
LORA_SYNC = "12"     # Sync word in hex (12 = 0x12)

def send_command(ser, cmd, delay=0.5):
    """Send a command and wait"""
    print(f"Sending: {cmd}")
    ser.write((cmd + '\n').encode())
    time.sleep(delay)

def configure_gateway():
    print(f"Connecting to {PORT}...")
    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        time.sleep(2)  # Wait for connection
        
        # Enter config menu
        send_command(ser, 'c', 1)
        
        # Configure WiFi (Option 1)
        print("\n=== Configuring WiFi ===")
        send_command(ser, '1', 0.5)
        send_command(ser, 'y', 0.5)  # Enable WiFi
        send_command(ser, WIFI_SSID, 0.5)
        send_command(ser, WIFI_PASSWORD, 1)
        
        # Configure MQTT (Option 2)
        print("\n=== Configuring MQTT ===")
        send_command(ser, '2', 0.5)
        send_command(ser, 'y', 0.5)  # Enable MQTT
        send_command(ser, MQTT_SERVER, 0.5)
        send_command(ser, MQTT_PORT, 0.5)
        send_command(ser, MQTT_USER, 0.5)
        send_command(ser, MQTT_PASS, 0.5)
        send_command(ser, '', 0.3)  # Client ID (default)
        send_command(ser, '', 0.3)  # Topic prefix (default)
        send_command(ser, 'y', 0.3)  # Publish raw
        send_command(ser, 'y', 0.3)  # Publish decoded
        send_command(ser, 'y', 1)    # Subscribe commands
        
        # Configure LoRa (Option 3)
        print("\n=== Configuring LoRa ===")
        send_command(ser, '3', 0.5)
        send_command(ser, LORA_FREQ, 0.5)
        send_command(ser, LORA_BW, 0.5)
        send_command(ser, LORA_SF, 0.5)
        send_command(ser, LORA_CR, 0.5)
        send_command(ser, LORA_POWER, 0.5)
        send_command(ser, LORA_SYNC, 0.5)
        send_command(ser, 'y', 1)  # Enable CRC
        
        # Save configuration (Option 6)
        print("\n=== Saving Configuration ===")
        send_command(ser, '6', 1)
        
        # Restart (Option 8)
        print("\n=== Restarting Device ===")
        send_command(ser, '8', 1)
        
        print("\nConfiguration complete!")
        print("Device is restarting with new settings...")
        
        # Read responses for a few seconds
        time.sleep(3)
        while ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
            except:
                pass

if __name__ == '__main__':
    try:
        configure_gateway()
    except serial.SerialException as e:
        print(f"Error: {e}")
        print("Make sure the serial monitor is closed!")
    except KeyboardInterrupt:
        print("\nAborted by user")

