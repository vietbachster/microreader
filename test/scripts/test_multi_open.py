#!/usr/bin/env python3
"""Delete all MRBs, then open each book (with conversion) until one fails."""
import serial
import time
import sys

PORT = "COM4"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=2)
time.sleep(1)


def drain():
    lines = []
    while ser.in_waiting:
        raw = ser.readline().decode("utf-8", errors="replace").strip()
        if raw:
            lines.append(raw)
    return lines


def send_status(label=""):
    ser.write(b"CMNDS")
    time.sleep(0.5)
    lines = drain()
    if label:
        print(f"[{label}]")
    for l in lines:
        print(f"  {l}")
    return lines


def clear_mrbs():
    print("--- clearing all .mrb files ---")
    ser.write(b"CMNDC")
    time.sleep(1)
    for l in drain():
        print(f"  {l}")


def list_books():
    ser.write(b"CMNDL")
    time.sleep(1)
    lines = drain()
    epubs = []
    for l in lines:
        if l.endswith(".epub"):
            epubs.append(l)
    return epubs


def open_book(path, timeout=120):
    data = path.encode()
    ser.write(b"CMND" + b"O" + len(data).to_bytes(2, "little") + data)
    time.sleep(0.5)
    print(f"--- opening {path} ---")
    start = time.time()
    result = None
    while time.time() - start < timeout:
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                print(f"  {line}")
                if "BOOK_OK" in line:
                    result = True
                    break
                if "BOOK_FAIL" in line or "abort" in line or "task_wdt" in line:
                    result = False
                    break
    # Drain remaining
    time.sleep(0.5)
    for l in drain():
        print(f"  {l}")
    if result is None:
        print("  TIMEOUT")
        return False
    return result


def press_back():
    print("  [pressing back]")
    ser.write(b"CMNDB" + bytes([1 << 0]))
    time.sleep(1.5)
    for l in drain():
        print(f"  {l}")


# Drain boot messages
time.sleep(1)
drain()

send_status("initial status")

# Step 1: Clear all .mrb files
clear_mrbs()
send_status("after clear")

# Step 2: Get list of .epub files
epubs = list_books()
print(f"\nFound {len(epubs)} epub files: {epubs}\n")

# Step 3: Open each book (will need conversion since MRBs are deleted)
results = {}
for epub in epubs:
    name = epub.replace(".epub", "")
    ok = open_book(f"/sdcard/books/{epub}")
    results[name] = ok
    if ok:
        press_back()
    send_status(f"after {name}")
    if not ok:
        print(f"\n*** FAILED on: {name} ***")
        break

print(f"\n=== SUMMARY ===")
for name, ok in results.items():
    print(f'  {name}: {"OK" if ok else "FAIL"}')

ser.close()
