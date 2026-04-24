#!/usr/bin/env python3
"""
Convert a PNG image to Microreader grayscale layers (BW, LSB, MSB).
Outputs 3 PNGs for debugging and a C++ header file with packed binary data.

Usage:
    python png_to_grayscale_layers.py input.png --out-prefix outname

Requires: Pillow
"""
import sys
import os
import argparse
from PIL import Image
import numpy as np


def quantize_5level(gray):
    """Quantize 0-255 grayscale to 5 levels (0=white, 1=light, 2=gray, 3=dark, 4=black)."""
    # These thresholds match the font generator's convention
    # 0-51: white (0), 52-102: light (1), 103-153: gray (2), 154-204: dark (3), 205-255: black (4)
    # But font generator likely uses even buckets, so:
    # 0-51, 52-102, 103-153, 154-204, 205-255
    # Cast to int to avoid uint8 overflow
    gray_int = gray.astype(int)
    return np.clip((255 - gray_int) * 5 // 256, 0, 4).astype(np.uint8)


def main():
    parser = argparse.ArgumentParser(
        description="Convert PNG to Microreader grayscale layers."
    )
    parser.add_argument("input", help="Input PNG image")
    parser.add_argument(
        "--out-prefix", default="output", help="Prefix for output files"
    )
    args = parser.parse_args()

    img = Image.open(args.input).convert("L")
    arr = np.array(img)
    levels = quantize_5level(arr)

    # BW: 1 if black/gray/dark, 0 otherwise (levels 2,3,4)
    bw = ((levels >= 2).astype(np.uint8)) * 255
    # LSB: 1 if light/dark, 0 otherwise (levels 1,3)
    lsb = (((levels == 1) | (levels == 3)).astype(np.uint8)) * 255
    # MSB: 1 if gray/dark, 0 otherwise (levels 2,3)
    msb = (((levels == 2) | (levels == 3)).astype(np.uint8)) * 255

    # Save debug PNGs
    Image.fromarray(bw).save(f"{args.out_prefix}_bw.png")
    Image.fromarray(lsb).save(f"{args.out_prefix}_lsb.png")
    Image.fromarray(msb).save(f"{args.out_prefix}_msb.png")
    print(f"Saved {args.out_prefix}_bw.png, _lsb.png, _msb.png")

    # Pack bits into bytes (row-major, left-to-right, top-to-bottom)
    def pack_bits(arr):
        h, w = arr.shape
        row_stride = (w + 7) // 8
        out = bytearray(row_stride * h)
        for y in range(h):
            for x in range(w):
                if arr[y, x]:
                    out[y * row_stride + x // 8] |= 1 << (7 - (x % 8))
        return out

    bw_bytes = pack_bits(bw)
    lsb_bytes = pack_bits(lsb)
    msb_bytes = pack_bits(msb)

    # Write C++ header
    header_path = f"{args.out_prefix}_grayscale.h"
    var_base = os.path.basename(args.out_prefix)
    with open(header_path, "w") as f:
        f.write("#pragma once\n\n")
        f.write(f"// Auto-generated from {args.input}\n")
        f.write(f"constexpr int {var_base}_width = {img.width};\n")
        f.write(f"constexpr int {var_base}_height = {img.height};\n\n")
        for name, data in zip(["bw", "lsb", "msb"], [bw_bytes, lsb_bytes, msb_bytes]):
            f.write(f"constexpr unsigned char {var_base}_{name}[] = {{\n    ")
            for i, b in enumerate(data):
                f.write(f"0x{b:02x}, ")
                if (i + 1) % 16 == 0:
                    f.write("\n    ")
            f.write("\n};\n\n")
    print(f"Wrote {header_path}")


if __name__ == "__main__":
    main()
