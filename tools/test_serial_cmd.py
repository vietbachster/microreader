#!/usr/bin/env python3
"""Quick non-interactive test of the serial command protocol."""
import serial
import struct
import time
import sys

MAGIC = b"CMND"
PORT = "COM4"
BAUD = 115200


def main():
    print(f"Opening {PORT}...")
    ser = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(1)  # wait for device boot
    ser.reset_input_buffer()

    # Test 1: Status
    print("\n=== STATUS ===")
    ser.write(MAGIC + b"S")
    deadline = time.time() + 5
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        print(f"  {line}")
        if line.startswith("STATUS:") or line.startswith("ERR:"):
            break

    time.sleep(0.3)
    ser.reset_input_buffer()

    # Test 2: List books
    print("\n=== LIST BOOKS ===")
    ser.write(MAGIC + b"L")
    deadline = time.time() + 5
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        print(f"  {line}")
        if line == "END" or line.startswith("ERR:"):
            break

    time.sleep(0.3)
    ser.reset_input_buffer()

    # Test 3: Button press (btn 0 = back, harmless on menu)
    print("\n=== BUTTON (back) ===")
    ser.write(MAGIC + b"B" + bytes([1 << 0]))
    deadline = time.time() + 3
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        print(f"  {line}")
        if line.startswith("OK") or line.startswith("ERR:"):
            break

    ser.close()
    print("\n=== ALL TESTS DONE ===")


if __name__ == "__main__":
    main()
