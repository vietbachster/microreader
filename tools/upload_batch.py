#!/usr/bin/env python3
"""Upload multiple EPUB files to the microreader2 device over serial.

Usage:
    python upload_batch.py [--port COM4] [--baud 115200]
"""
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
UPLOAD_SCRIPT = SCRIPT_DIR / "upload_epub.py"
BOOKS_ROOT = SCRIPT_DIR.parent / "test" / "books"

# Representative set of test books — diverse sizes and languages.
# Total ~5MB, roughly 15-30min at serial upload speeds.
BOOKS = [
    # Tiny (< 200KB)
    "gutenberg/metamorphosis-kafka.epub",  # 119KB, 6 chapters
    "gutenberg/paradise-lost.epub",  # 85KB, poetry
    "gutenberg/heart-darkness.epub",  # 164KB, novella
    "gutenberg/alice-wonderland.epub",  # 185KB, classic
    "gutenberg/faust-de.epub",  # 193KB, German
    # Small (200-500KB)
    "gutenberg/jekyll-hyde.epub",  # 298KB
    "gutenberg/sherlock-adventures.epub",  # 372KB
    "gutenberg/frankenstein.epub",  # 465KB
    # Medium (500KB-1MB)
    "gutenberg/dracula.epub",  # 590KB
    "gutenberg/moby-dick.epub",  # 797KB, long chapters
    # Large (1-2MB)
    "gutenberg/anna-karenina.epub",  # 1010KB
    "gutenberg/war-and-peace.epub",  # 1804KB, 368 chapters
]


def main():
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--max-size-mb",
        type=float,
        default=3.0,
        help="Skip books larger than this (MB)",
    )
    args = parser.parse_args()

    results = []
    for rel_path in BOOKS:
        epub = BOOKS_ROOT / rel_path
        if not epub.exists():
            print(f"SKIP (not found): {rel_path}")
            results.append((rel_path, "NOT FOUND"))
            continue
        size_mb = epub.stat().st_size / (1024 * 1024)
        if size_mb > args.max_size_mb:
            print(f"SKIP (too large: {size_mb:.1f}MB): {rel_path}")
            results.append((rel_path, f"TOO LARGE ({size_mb:.1f}MB)"))
            continue
        print(f"\n{'='*60}")
        print(f"Uploading: {epub.name} ({size_mb:.1f}MB)")
        print(f"{'='*60}")
        ret = subprocess.run(
            [
                sys.executable,
                str(UPLOAD_SCRIPT),
                str(epub),
                "--port",
                args.port,
                "--baud",
                str(args.baud),
            ],
            timeout=600,
        )
        if ret.returncode == 0:
            results.append((rel_path, "OK"))
        else:
            results.append((rel_path, "FAILED"))

    print(f"\n{'='*60}")
    print("UPLOAD SUMMARY")
    print(f"{'='*60}")
    for path, status in results:
        print(f"  {status:12s}  {path}")


if __name__ == "__main__":
    main()
