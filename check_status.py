#!/usr/bin/env python3
import serial
import time

PORT = 'COM7'
BAUD = 115200

print(f"Connecting to {PORT}...")
try:
    with serial.Serial(PORT, BAUD, timeout=2) as ser:
        time.sleep(2)  # Wait for connection
        
        # Clear any existing data
        ser.reset_input_buffer()
        
        # Send 's' command to show statistics
        print("\nSending 's' command to show status...")
        ser.write(b's\n')
        time.sleep(1)
        
        # Read all available data
        print("\n" + "="*60)
        print("GATEWAY STATUS:")
        print("="*60)
        
        lines_read = 0
        timeout_counter = 0
        while timeout_counter < 30:  # Read for 3 seconds
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                        lines_read += 1
                        timeout_counter = 0  # Reset timeout when we get data
                except Exception as e:
                    pass
            else:
                time.sleep(0.1)
                timeout_counter += 1
        
        print("="*60)
        print(f"\nRead {lines_read} lines from serial port")
        
        if lines_read == 0:
            print("\nNo response received. Trying to read boot messages...")
            ser.write(b'\n')
            time.sleep(1)
            while ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                except:
                    pass

except serial.SerialException as e:
    print(f"Error: {e}")
    print("Make sure:")
    print("  1. The device is plugged into COM7")
    print("  2. No other serial monitors are open")
    print("  3. The device has power")
except KeyboardInterrupt:
    print("\nAborted by user")

