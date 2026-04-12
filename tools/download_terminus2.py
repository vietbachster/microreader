#!/usr/bin/env python3
"""Download Terminus-Bold TTF font."""
import os, urllib.request, zipfile

dest_dir = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "resources", "fonts"
)
os.makedirs(dest_dir, exist_ok=True)
dest = os.path.join(dest_dir, "Terminus-Bold.ttf")

url = "https://files.ax86.net/terminus-ttf/files/4.49.3/terminus-ttf-4.49.3.zip"
zip_path = os.path.join(dest_dir, "terminus-ttf.zip")
print(f"Downloading {url}...")
urllib.request.urlretrieve(url, zip_path)

with zipfile.ZipFile(zip_path) as z:
    for name in z.namelist():
        if name.endswith("TerminusTTF-Bold-4.49.3.ttf"):
            with z.open(name) as src, open(dest, "wb") as dst:
                dst.write(src.read())
            print(f"Extracted: {dest} ({os.path.getsize(dest)} bytes)")
            break
    else:
        print("ERROR: TerminusTTF-Bold not found in zip. Contents:")
        for name in z.namelist():
            print(f"  {name}")

os.remove(zip_path)
