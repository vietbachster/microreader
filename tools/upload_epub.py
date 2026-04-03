#!/usr/bin/env python3
"""Upload an EPUB file to the microreader2 device over serial.

Usage:
    python upload_epub.py <epub_file> [--port COM4] [--baud 115200]
"""
import argparse
import struct
import sys
import time
import zlib
from pathlib import Path

import serial


def upload(port: str, baud: int, filepath: Path) -> bool:
    data = filepath.read_bytes()
    name = filepath.name.encode("utf-8")
    crc = zlib.crc32(data) & 0xFFFFFFFF

    print(f"Uploading {filepath.name} ({len(data)} bytes, CRC32=0x{crc:08x})")

    with serial.Serial(port, baud, timeout=10) as ser:
        # Send magic + name_len + name + file_size
        header = b"EPUB"
        header += struct.pack("<H", len(name))
        header += name
        header += struct.pack("<I", len(data))
        ser.write(header)

        # Wait for READY (may be preceded by log lines)
        deadline = time.time() + 10
        while time.time() < deadline:
            resp = ser.readline().decode("utf-8", errors="replace").strip()
            if not resp:
                continue
            if resp == "READY":
                break
            if resp.startswith("ERR:"):
                print(f"Upload failed: {resp!r}")
                return False
            # Skip any other output (ESP log lines, app log lines, etc.)
            continue
        else:
            print("Timeout waiting for READY")
            return False
        print("Device ready, sending data...")

        # Send payload in chunks with flow control.
        # Device sends ACK (0x06) after writing each chunk to SD.
        chunk_size = 2048
        sent = 0
        t0 = time.time()
        while sent < len(data):
            end = min(sent + chunk_size, len(data))
            ser.write(data[sent:end])
            sent = end
            # Wait for ACK byte, skipping any interleaved log output
            deadline_ack = time.time() + 30
            got_ack = False
            while time.time() < deadline_ack:
                b = ser.read(1)
                if b == b"\x06":
                    got_ack = True
                    break
                # Any other byte is log output — ignore it
            if not got_ack:
                print("\nTimeout waiting for ACK")
                return False
            elapsed = time.time() - t0
            rate = sent / elapsed / 1024 if elapsed > 0 else 0
            pct = sent * 100 // len(data)
            print(
                f"\r  {sent}/{len(data)} bytes ({pct}%) {rate:.0f} KB/s",
                end="",
                flush=True,
            )
        print()

        # Send CRC
        ser.write(struct.pack("<I", crc))

        # Wait for result (may be preceded by log lines)
        deadline = time.time() + 30
        while time.time() < deadline:
            resp = ser.readline().decode("utf-8", errors="replace").strip()
            if not resp:
                continue
            if resp == "OK":
                print("Upload successful!")
                return True
            if resp.startswith("ERR:"):
                print(f"Upload failed: {resp!r}")
                return False
            # Skip any other output (ESP log lines, app log lines, etc.)
            continue
        print("Timeout waiting for result")
        return False


def main():
    parser = argparse.ArgumentParser(description="Upload EPUB to microreader2")
    parser.add_argument("file", type=Path, help="EPUB file to upload")
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    args = parser.parse_args()

    if not args.file.exists():
        print(f"File not found: {args.file}")
        sys.exit(1)

    if not upload(args.port, args.baud, args.file):
        sys.exit(1)


if __name__ == "__main__":
    main()
