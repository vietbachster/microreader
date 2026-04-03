#!/usr/bin/env python3
"""Capture serial output to a file. Resets the ESP32 via DTR toggle to trigger
a fresh benchmark run.

Usage:
    python serial_capture.py [--port COM4] [--baud 115200] [--output bench.log]
"""
import argparse
import sys
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", default="bench.log")
    parser.add_argument(
        "--timeout",
        type=int,
        default=300,
        help="Stop after this many seconds (default 300)",
    )
    args = parser.parse_args()

    out = open(args.output, "w", encoding="utf-8")
    print(f"Capturing {args.port} → {args.output} (timeout={args.timeout}s)")

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        # Toggle DTR/RTS to reset the ESP32.
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.1)
        # Flush any garbage.
        ser.reset_input_buffer()

        t0 = time.time()
        while time.time() - t0 < args.timeout:
            line = ser.readline().decode("utf-8", errors="replace")
            if not line:
                continue
            line = line.rstrip("\r\n")
            print(line)
            out.write(line + "\n")
            out.flush()
            # Stop early if we see the DONE marker.
            if "=== DONE:" in line:
                print("\n--- Benchmark complete ---")
                break
        else:
            print("\n--- Timeout ---")

    out.close()
    print(f"Output saved to {args.output}")


if __name__ == "__main__":
    main()
