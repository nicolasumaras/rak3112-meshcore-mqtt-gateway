#!/usr/bin/env python3
import serial
import time

PORT = 'COM7'
BAUD = 115200

print("Connecting to COM7...\n")
try:
    with serial.Serial(PORT, BAUD, timeout=1) as ser:
        time.sleep(1)
        ser.reset_input_buffer()
        
        # Read current output
        print("Current output:")
        print("-" * 60)
        start = time.time()
        while time.time() - start < 2:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
            time.sleep(0.05)
        
        print("-" * 60)
        print("\nSending 'c' to enter config menu...")
        ser.write(b'c\n')
        time.sleep(0.5)
        
        print("Sending '5' to show configuration...")
        ser.write(b'5\n')
        time.sleep(2)
        
        print("\n" + "=" * 60)
        print("CURRENT CONFIGURATION:")
        print("=" * 60)
        
        config_text = []
        timeout = 0
        while timeout < 40:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
                    config_text.append(line.lower())
                    timeout = 0
            else:
                time.sleep(0.1)
                timeout += 1
        
        print("=" * 60)
        
        # Exit config
        ser.write(b'0\n')
        time.sleep(0.3)
        
        # Check what we found
        print("\nDIAGNOSTICS:")
        print("-" * 60)
        
        wifi_enabled = any('wifi' in line and 'enabled' in line and 'yes' in line for line in config_text)
        trucell_found = any('trucell' in line for line in config_text)
        freq_915_8 = any('915.8' in line or '915.80' in line for line in config_text)
        
        print(f"WiFi Enabled: {'YES' if wifi_enabled else 'NO or UNKNOWN'}")
        print(f"SSID 'Trucell' found: {'YES' if trucell_found else 'NO'}")
        print(f"Frequency 915.8: {'YES' if freq_915_8 else 'NO'}")
        
        if not wifi_enabled:
            print("\nWARNING: WiFi may not be enabled!")
            print("The configuration may not have been saved properly.")
        
        if not trucell_found:
            print("\nWARNING: WiFi SSID 'Trucell Signage' not found!")
            print("Configuration may have failed.")

except serial.SerialException as e:
    print(f"ERROR: Cannot open COM7")
    print(f"Details: {e}")
    print("\nClose any other programs using COM7 (serial monitors, etc.)")
except KeyboardInterrupt:
    print("\nAborted")

