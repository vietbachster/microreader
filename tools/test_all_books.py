#!/usr/bin/env python3
"""Open every book on the device and report which ones load successfully.

Sends CMND-protocol commands to list books, then opens each one and
watches serial output for BOOK_OK or BOOK_FAIL log messages.

Two modes:
  --clean    Delete all .mrb files first, then open .epub files
             (tests full conversion pipeline). Default timeout: 120s.
  (default)  Open existing .mrb files (tests just the reader).
             Default timeout: 30s.

Usage:
    python test_all_books.py [--port COM4] [--clean] [-v] [--filter alice]
"""
import argparse
import struct
import sys
import time

import serial

MAGIC = b"CMND"


def drain(ser: serial.Serial):
    """Drain any pending serial data."""
    ser.timeout = 0.1
    while ser.read(4096):
        pass
    ser.timeout = 1


def send_list_books(ser: serial.Serial) -> list[str]:
    drain(ser)
    ser.write(MAGIC + b"L")
    deadline = time.time() + 5.0
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
        if started:
            result.append(line)
    return result


def send_open(ser: serial.Serial, path: str):
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)


def send_button(ser: serial.Serial, index: int):
    ser.write(MAGIC + b"B" + bytes([1 << index]))


def send_clear_mrb(ser: serial.Serial) -> str:
    """Send 'C' command to delete all .mrb files. Returns 'CLEARED:N' or error."""
    drain(ser)
    ser.write(MAGIC + b"C")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("CLEARED:") or line.startswith("ERR:"):
            return line
    return "TIMEOUT"


def wait_for_book_result(
    ser: serial.Serial, timeout: float, verbose: bool = False
) -> str | None:
    """Watch serial output for BOOK_OK or BOOK_FAIL. Returns the matching line or None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if verbose:
            print(f"  | {line}")
        if "BOOK_OK:" in line:
            return "OK"
        if "BOOK_FAIL:" in line:
            return "FAIL"
    return None


def main():
    parser = argparse.ArgumentParser(
        description="Test all books on microreader2 device"
    )
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--timeout",
        type=int,
        default=None,
        help="Max seconds per book (default: 120 with --clean, 30 otherwise)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete all .mrb files first, test full epub→mrb conversion",
    )
    parser.add_argument(
        "--filter", default=None, help="Only test books matching this substring"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print all serial output"
    )
    args = parser.parse_args()
    if args.timeout is None:
        args.timeout = 120 if args.clean else 30

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Cannot open {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Connected to {args.port} @ {args.baud}")
    time.sleep(2)  # let device boot
    drain(ser)

    # Optionally delete all .mrb files for conversion testing
    if args.clean:
        print("Clearing .mrb files ... ", end="", flush=True)
        result = send_clear_mrb(ser)
        print(result)
        if result.startswith("ERR"):
            ser.close()
            sys.exit(1)
        time.sleep(1)

    # Get book list
    books = send_list_books(ser)
    if not books:
        print("No books found on device!")
        ser.close()
        sys.exit(1)

    if args.clean:
        # Conversion mode: test .epub files only
        test_books = sorted(b for b in books if b.endswith(".epub"))
        mode = "conversion"
    else:
        # Open mode: prefer .mrb, fall back to .epub
        mrb_set = {b for b in books if b.endswith(".mrb")}
        epub_set = {b for b in books if b.endswith(".epub")}
        test_books = []
        for epub in sorted(epub_set):
            mrb = epub.rsplit(".", 1)[0] + ".mrb"
            if mrb in mrb_set:
                test_books.append(mrb)
            else:
                test_books.append(epub)
        for mrb in sorted(mrb_set):
            epub = mrb.rsplit(".", 1)[0] + ".epub"
            if epub not in epub_set:
                test_books.append(mrb)
        mode = "open"

    if args.filter:
        test_books = [b for b in test_books if args.filter in b]

    print(
        f"\nTesting {len(test_books)} books ({mode} mode, {args.timeout}s timeout):\n"
    )

    results = {"OK": [], "FAIL": [], "TIMEOUT": []}

    for i, book in enumerate(test_books, 1):
        path = f"/sdcard/books/{book}"
        print(f"[{i}/{len(test_books)}] {book} ... ", end="", flush=True)

        drain(ser)
        send_open(ser, path)
        result = wait_for_book_result(ser, args.timeout, verbose=args.verbose)

        if result == "OK":
            print("OK")
            results["OK"].append(book)
        elif result == "FAIL":
            print("FAIL")
            results["FAIL"].append(book)
        else:
            print("TIMEOUT")
            results["TIMEOUT"].append(book)

        # Press back to return to menu
        time.sleep(0.5)
        send_button(ser, 0)  # back
        time.sleep(2.0)
        drain(ser)  # clear any remaining output from screen transition

    ser.close()

    # Summary
    print(f"\n{'='*50}")
    print(
        f"Results: {len(results['OK'])} OK, {len(results['FAIL'])} FAIL, {len(results['TIMEOUT'])} TIMEOUT"
    )
    if results["FAIL"]:
        print(f"\nFailed books:")
        for b in results["FAIL"]:
            print(f"  - {b}")
    if results["TIMEOUT"]:
        print(f"\nTimed out books:")
        for b in results["TIMEOUT"]:
            print(f"  - {b}")

    sys.exit(1 if results["FAIL"] or results["TIMEOUT"] else 0)


if __name__ == "__main__":
    main()
