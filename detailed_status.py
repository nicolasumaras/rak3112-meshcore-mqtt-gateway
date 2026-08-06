#!/usr/bin/env python3
import serial
import time

PORT = 'COM7'
BAUD = 115200

print(f"Connecting to {PORT}...")
try:
    with serial.Serial(PORT, BAUD, timeout=2) as ser:
        time.sleep(2)
        
        # Clear buffer
        ser.reset_input_buffer()
        
        print("\n" + "="*60)
        print("READING CURRENT OUTPUT (10 seconds)...")
        print("="*60 + "\n")
        
        # Just read whatever is coming for 10 seconds
        start_time = time.time()
        while time.time() - start_time < 10:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                except:
                    pass
            time.sleep(0.1)
        
        print("\n" + "="*60)
        print("Sending 's' command for statistics...")
        print("="*60 + "\n")
        
        ser.write(b's\n')
        time.sleep(2)
        
        # Read response
        timeout_counter = 0
        while timeout_counter < 30:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                        timeout_counter = 0
                except:
                    pass
            else:
                time.sleep(0.1)
                timeout_counter += 1
        
        print("\n" + "="*60)
        print("Sending 'c' then '5' to show configuration...")
        print("="*60 + "\n")
        
        ser.write(b'c\n')
        time.sleep(0.5)
        ser.write(b'5\n')
        time.sleep(2)
        
        # Read configuration
        timeout_counter = 0
        while timeout_counter < 50:
            if ser.in_waiting:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                        timeout_counter = 0
                except:
                    pass
            else:
                time.sleep(0.1)
                timeout_counter += 1
        
        # Exit config menu
        ser.write(b'0\n')
        time.sleep(0.5)

except serial.SerialException as e:
    print(f"Error: {e}")
except KeyboardInterrupt:
    print("\nAborted by user")

