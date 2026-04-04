#!/usr/bin/env python3
"""Interactive serial commander for microreader2 device.

Sends CMND-protocol commands over USB serial to control the device UI,
inject button presses, query status, list books, and open books.

Usage:
    python serial_cmd.py [--port COM4] [--baud 115200]

Commands (interactive):
    btn <N>           Press button N (0=back, 1=select, 2=down/next, 3=up/prev, 4=up, 5=down, 6=power)
    back              Alias for btn 0
    select / sel      Alias for btn 1
    down / next       Alias for btn 2
    up / prev         Alias for btn 3
    status / st       Query heap status
    books / ls        List books on SD card
    open <path>       Open book by full path (e.g. /sdcard/books/alice.epub)
    open <name>       Open book by filename (auto-prepends /sdcard/books/)
    help              Show this help
    quit / exit       Exit
"""
import argparse
import struct
import sys
import time

import serial

MAGIC = b"CMND"


def send_button(ser: serial.Serial, mask: int) -> str:
    ser.write(MAGIC + b"B" + bytes([mask & 0xFF]))
    return read_response(ser)


def send_status(ser: serial.Serial) -> str:
    ser.write(MAGIC + b"S")
    return read_response(ser)


def send_list_books(ser: serial.Serial) -> str:
    ser.write(MAGIC + b"L")
    return read_multiline_response(ser)


def send_open(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser)


def read_response(ser: serial.Serial, timeout: float = 3.0) -> str:
    """Read lines until we get OK, ERR:, or STATUS: response."""
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if (
            line.startswith("OK")
            or line.startswith("ERR:")
            or line.startswith("STATUS:")
        ):
            return line
        # Skip ESP log lines
        lines.append(line)
    return "TIMEOUT (no response)"


def read_multiline_response(ser: serial.Serial, timeout: float = 5.0) -> str:
    """Read lines until END marker."""
    deadline = time.time() + timeout
    result = []
    started = False
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("BOOKS:"):
            started = True
            continue
        if line == "END":
            break
        if line.startswith("ERR:"):
            return line
        if started:
            result.append(line)
        # Skip ESP log lines before BOOKS:
    return "\n".join(result) if result else "(no books found)"


BUTTON_ALIASES = {
    "back": 0,
    "select": 1,
    "sel": 1,
    "down": 2,
    "next": 2,
    "up": 3,
    "prev": 3,
}


def main():
    parser = argparse.ArgumentParser(description="Serial commander for microreader2")
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Cannot open {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {args.port} @ {args.baud}")
    print("Type 'help' for commands, 'quit' to exit.\n")

    try:
        while True:
            try:
                cmd = input("> ").strip()
            except EOFError:
                break
            if not cmd:
                continue

            parts = cmd.split(maxsplit=1)
            verb = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ""

            if verb in ("quit", "exit", "q"):
                break
            elif verb == "help":
                print(__doc__)
            elif verb == "btn":
                try:
                    mask = 1 << int(arg)
                    print(send_button(ser, mask))
                except (ValueError, IndexError):
                    print("Usage: btn <0-6>")
            elif verb in BUTTON_ALIASES:
                mask = 1 << BUTTON_ALIASES[verb]
                print(send_button(ser, mask))
            elif verb in ("status", "st"):
                print(send_status(ser))
            elif verb in ("books", "ls"):
                print(send_list_books(ser))
            elif verb == "open":
                if not arg:
                    print("Usage: open <path_or_filename>")
                    continue
                path = arg
                if not path.startswith("/"):
                    path = f"/sdcard/books/{path}"
                print(send_open(ser, path))
            else:
                print(f"Unknown command: {verb!r}. Type 'help' for commands.")
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()
        print("Disconnected.")


if __name__ == "__main__":
    main()
