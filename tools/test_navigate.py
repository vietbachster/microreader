#!/usr/bin/env python3
"""Navigate 50+ pages on the real device and verify no crash.

Usage: python tools/test_navigate.py [--port COM4] [--book ohler.epub] [--pages 60]
"""
import argparse
import struct
import sys
import time

import serial

MAGIC = b"CMND"


def send_button(ser, mask):
    ser.write(MAGIC + b"B" + bytes([mask & 0xFF]))
    return read_response(ser)


def send_status(ser):
    ser.write(MAGIC + b"S")
    return read_response(ser)


def send_open(ser, path):
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser, timeout=120.0)  # Longer timeout for MRB conversion


def send_clear(ser):
    ser.write(MAGIC + b"C")
    return read_response(ser, timeout=10.0)


def read_response(ser, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if (
            line.startswith("OK")
            or line.startswith("ERR:")
            or line.startswith("STATUS:")
            or line.startswith("CLEARED:")
            or line.startswith("BOOK_OK")
            or line.startswith("BOOK_FAIL")
        ):
            return line
    return "TIMEOUT"


def drain(ser, duration=0.5):
    """Drain any pending serial data."""
    deadline = time.time() + duration
    while time.time() < deadline:
        ser.readline()


def main():
    parser = argparse.ArgumentParser(description="Navigate pages on real device")
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--book", default="ohler.epub")
    parser.add_argument("--pages", type=int, default=60)
    parser.add_argument(
        "--delay",
        type=float,
        default=0.1,
        help="Delay between button presses in seconds (simulates fast reading)",
    )
    parser.add_argument(
        "--clean", action="store_true", help="Clear .mrb files before opening"
    )
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=2)
    time.sleep(2)  # Wait for device to settle after connection
    drain(ser, 1.0)

    # Status check
    resp = send_status(ser)
    print(f"Initial status: {resp}")
    if resp == "TIMEOUT":
        print("ERROR: Device not responding!")
        ser.close()
        sys.exit(1)

    # Clear .mrb if requested
    if args.clean:
        print("Clearing .mrb files...")
        resp = send_clear(ser)
        print(f"  {resp}")

    # Open the book
    path = args.book if args.book.startswith("/") else f"/sdcard/books/{args.book}"
    print(f"\nOpening: {path}")
    resp = send_open(ser, path)
    print(f"  Response: {resp}")
    if "BOOK_FAIL" in resp or "ERR" in resp:
        print("ERROR: Failed to open book!")
        ser.close()
        sys.exit(1)

    # Wait for book to load and render first page
    print("Waiting for first page to render...")
    time.sleep(5)
    # Drain and print any crash/log output
    deadline = time.time() + 2
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(f"  [device] {line}")

    # Get baseline heap (right after opening book)
    drain(ser, 0.2)
    baseline = send_status(ser)
    print(f"Baseline after open: {baseline}")
    # Parse initial free heap from STATUS:free=NNN,largest=NNN
    init_free = None
    if baseline.startswith("STATUS:free="):
        try:
            init_free = int(baseline.split("free=")[1].split(",")[0])
        except (ValueError, IndexError):
            pass

    # Navigate forward
    BTN_DOWN = 1 << 2  # button 2 = next page
    print(f"\nNavigating {args.pages} pages (delay={args.delay}s between presses)...")
    failures = 0
    crashed = False
    for i in range(1, args.pages + 1):
        resp = send_button(ser, BTN_DOWN)
        if resp == "TIMEOUT":
            failures += 1
            print(f"  Page {i}: TIMEOUT (device may have crashed!)")
            if failures >= 3:
                print("ERROR: 3 consecutive timeouts — device likely crashed!")
                crashed = True
                break
        else:
            failures = 0  # Reset on success
            if i % 10 == 0:
                # Check status every 10 pages
                drain(ser, 0.2)
                status = send_status(ser)
                print(f"  Page {i}: {status}")
                # Detect reboot: if free heap jumps back to boot level (>170KB)
                # when it was lower before, device rebooted
                if init_free and status.startswith("STATUS:free="):
                    try:
                        cur_free = int(status.split("free=")[1].split(",")[0])
                        if cur_free > init_free + 20000:
                            print(
                                f"  ERROR: Heap jumped from {init_free} to {cur_free} — device rebooted!"
                            )
                            crashed = True
                            break
                    except (ValueError, IndexError):
                        pass
        time.sleep(args.delay)

    # Final status
    drain(ser, 0.5)
    resp = send_status(ser)
    print(f"\nFinal status: {resp}")

    # Leave the book open so the user can see it on the device
    ser.close()

    if failures >= 3 or crashed:
        print("\nRESULT: FAIL — device crashed or stopped responding!")
        sys.exit(1)
    else:
        print(f"\nRESULT: PASS — navigated {args.pages} pages successfully")
        sys.exit(0)


if __name__ == "__main__":
    main()
