import zipfile, re

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)
xhtml = [n for n in z.namelist() if n.endswith((".xhtml", ".html", ".htm"))]

# Search for Rechtsnach in all files
print("=== Searching for 'Rechtsnach' ===")
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    if "Rechtsnach" in data:
        for m in re.finditer(r"Rechtsnach\S*", data):
            ctx_start = max(0, m.start() - 80)
            ctx_end = min(len(data), m.end() + 80)
            snippet = data[ctx_start:ctx_end].replace("\n", " ")
            print(f"  {f}:")
            print(f'    "{snippet}"')
            # Show hex of the matched word
            word = m.group()
            raw = word.encode("utf-8")
            hexstr = " ".join(f"{b:02X}" for b in raw)
            print(f'    Word: "{word}" -> [{hexstr}]')
            print()

# Also look for German words with special chars near "Rechts"
print("\n=== All occurrences of 'Rechts' ===")
count = 0
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    for m in re.finditer(r"\bRechts\S*", text):
        word = m.group()
        if any(ord(c) > 127 for c in word):
            raw = word.encode("utf-8")
            hexstr = " ".join(f"{b:02X}" for b in raw)
            print(f'  {f}: "{word}" -> [{hexstr}]')
            count += 1
    if count >= 20:
        break

if count == 0:
    print("  No Rechts* words with non-ASCII found")
    # Just list all Rechts* words
    for f in xhtml:
        data = z.read(f).decode("utf-8", errors="replace")
        text = re.sub(r"<[^>]+>", " ", data)
        for m in re.finditer(r"\bRechts\S*", text):
            print(f'  {f}: "{m.group()}"')
