#!/usr/bin/env python3
import serial
import time

PORT = 'COM7'
BAUD = 115200

print("Connecting to COM7 to check WiFi status...\n")
try:
    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        time.sleep(1)
        ser.reset_input_buffer()
        
        # Read any current output for 3 seconds
        print("Reading current output...")
        print("-" * 60)
        start = time.time()
        found_wifi = False
        found_mqtt = False
        found_lora = False
        
        while time.time() - start < 3:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    if 'WiFi' in line or 'wifi' in line:
                        found_wifi = True
                    if 'MQTT' in line or 'mqtt' in line:
                        found_mqtt = True
                    if 'LoRa' in line or 'MHz' in line:
                        found_lora = True
            time.sleep(0.05)
        
        print("-" * 60)
        
        # Send 'c' then '5' to show configuration
        print("\nChecking saved configuration...")
        ser.write(b'c\n')
        time.sleep(0.3)
        ser.write(b'5\n')
        time.sleep(1.5)
        
        print("-" * 60)
        config_lines = []
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                config_lines.append(line)
                print(line)
        
        print("-" * 60)
        
        # Exit config
        ser.write(b'0\n')
        time.sleep(0.3)
        
        # Analysis
        print("\n" + "=" * 60)
        print("ANALYSIS:")
        print("=" * 60)
        
        wifi_enabled = any('Enabled: Yes' in line or 'Enabled:                     Yes' in line for line in config_lines if 'WiFi' in line or line.strip().startswith('Enabled:'))
        
        if wifi_enabled:
            print("✓ WiFi is ENABLED in configuration")
        else:
            print("✗ WiFi appears to be DISABLED")
            
        if any('Trucell' in line for line in config_lines):
            print("✓ WiFi SSID 'Trucell Signage' is configured")
        else:
            print("✗ WiFi SSID may not be configured correctly")
            
        if any('915.8' in line or '915.80' in line for line in config_lines):
            print("✓ LoRa frequency 915.8 MHz is configured")
        else:
            print("✗ LoRa frequency may not be 915.8 MHz")
            
        if any('250' in line for line in config_lines):
            print("✓ LoRa bandwidth 250 kHz is configured")
        else:
            print("? LoRa bandwidth unclear")

except serial.SerialException as e:
    print(f"ERROR: {e}")
    print("\nTroubleshooting:")
    print("1. Close any other serial monitors")
    print("2. Unplug and replug the USB cable")
    print("3. Try a different USB port")
except Exception as e:
    print(f"ERROR: {e}")

