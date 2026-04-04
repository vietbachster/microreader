#!/usr/bin/env python3
"""List books on the device SD card."""
import serial
import time

ser = serial.Serial("COM4", 115200, timeout=2)
time.sleep(1)

# Drain boot messages
while ser.in_waiting:
    ser.readline()

# List books
ser.write(b"CMNDL")
time.sleep(1)

lines = []
while ser.in_waiting:
    line = ser.readline().decode("utf-8", errors="replace").strip()
    if line:
        lines.append(line)
        print(line)

ser.close()
