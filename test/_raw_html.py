import zipfile, re

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)

# Get the raw HTML around "Rechte" + "nachweis"
data = z.read("OEBPS/Text/content-10.xhtml").decode("utf-8", errors="replace")

# Find Bildnachweis area in raw HTML (with tags)
for term in ["Bildnachweis", "Rechtenachweis", "nachweis Nr"]:
    idx = data.find(term)
    if idx >= 0:
        start = max(0, idx - 100)
        end = min(len(data), idx + 200)
        snippet = data[start:end]
        print(f'=== Raw HTML around "{term}" (offset {idx}) ===')
        print(snippet)
        print()
        # Also show hex of just the interesting part
        raw = data[idx : idx + 80].encode("utf-8")
        hexstr = " ".join(f"{b:02X}" for b in raw)
        print(f"Hex: {hexstr}")
        print()

# Find ALL occurrences of "Rechte" near "nachweis" in raw HTML
print("\n=== All raw HTML matches for Rechte*nachweis ===")
for m in re.finditer(r"Rechte.{0,50}nachweis.{0,60}", data):
    print(f'  "{m.group()}"')
    raw = m.group().encode("utf-8")
    hexstr = " ".join(f"{b:02X}" for b in raw)
    print(f"  Hex: {hexstr}")
    print()
