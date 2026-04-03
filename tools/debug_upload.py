#!/usr/bin/env python3
"""Debug EPUB upload — sends protocol manually, shows all responses."""
import serial
import struct
import time
import zlib
from pathlib import Path

filepath = Path("test/fixtures/multi_chapter.epub")
data = filepath.read_bytes()
name = filepath.name.encode("utf-8")
crc = zlib.crc32(data) & 0xFFFFFFFF

print(f"Uploading {filepath.name} ({len(data)} bytes, CRC32=0x{crc:08x})")

s = serial.Serial("COM4", 115200, timeout=2)

# Drain buffered boot output — wait for device to be fully booted
print("Draining boot output...")
time.sleep(5)
while True:
    n = s.in_waiting
    if n == 0:
        break
    drained = s.read(n)
    print("[drain]", drained.decode("utf-8", errors="replace").rstrip()[:100])
    time.sleep(0.2)
print("Drain complete")

# Send header: magic + name_len + name + file_size
header = b"EPUB"
header += struct.pack("<H", len(name))
header += name
header += struct.pack("<I", len(data))
print(f"Sending header ({len(header)} bytes): {header[:20]}...")
s.write(header)
s.flush()

# Read response lines
print("Waiting for response...")
ready = False
for i in range(20):
    line = s.readline()
    if line:
        decoded = line.decode("utf-8", errors="replace").strip()
        print(f"  [{i}] {decoded!r}")
        if decoded == "READY":
            ready = True
            break
        if decoded.startswith("ERR:"):
            break
    else:
        print(f"  [{i}] (timeout)")

if ready:
    print("Got READY, sending data...")
    s.write(data)
    s.write(struct.pack("<I", crc))
    s.flush()
    time.sleep(1)
    # Read result
    for j in range(10):
        line = s.readline()
        if line:
            decoded = line.decode("utf-8", errors="replace").strip()
            print(f"  result[{j}] {decoded!r}")
            if decoded == "OK" or decoded.startswith("ERR:"):
                break
        else:
            print(f"  result[{j}] (timeout)")

s.close()
