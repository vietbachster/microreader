import zipfile, re

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)
xhtml = [n for n in z.namelist() if n.endswith((".xhtml", ".html", ".htm"))]

print("=== Searching for 'Rechtsnachweis' ===")
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    if "Rechtsnachweis" in data:
        for m in re.finditer(r".{0,80}Rechtsnachweis.{0,80}", data, re.DOTALL):
            snippet = m.group().replace("\n", "\\n")
            print(f"\n  {f}:")
            print(f'    Context: "{snippet}"')
            # Show hex of the area around "Rechtsnachweis"
            raw = m.group().encode("utf-8")
            # Find "Rechtsnachweis" in raw bytes
            idx = raw.find(b"Rechtsnachweis")
            if idx >= 0:
                start = max(0, idx - 10)
                end = min(len(raw), idx + 40)
                hexstr = " ".join(f"{b:02X}" for b in raw[start:end])
                chars = "".join(
                    chr(b) if 0x20 <= b < 0x7F else f"[{b:02X}]" for b in raw[start:end]
                )
                print(f"    Hex around match: {hexstr}")
                print(f"    Chars: {chars}")

# Also search for "Nr." near Rechtsnachweis
print("\n=== Searching for 'Nr.' near 'Rechtsnachweis' ===")
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    text = re.sub(r"\s+", " ", text)
    if "Rechtsnachweis" in text:
        for m in re.finditer(r".{0,50}Rechtsnachweis.{0,50}", text):
            snippet = m.group()
            raw = snippet.encode("utf-8")
            hexstr = " ".join(f"{b:02X}" for b in raw)
            print(f"\n  {f}:")
            print(f'    Text: "{snippet}"')
            print(f"    Hex:  {hexstr}")
            # Highlight non-ASCII bytes
            for i, b in enumerate(raw):
                if b > 0x7E:
                    print(f"    Non-ASCII at byte {i}: 0x{b:02X}")
