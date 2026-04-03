import zipfile, re, sys

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)
xhtml = [n for n in z.namelist() if n.endswith((".xhtml", ".html", ".htm"))]

# Search for 'Rechts' in all files
found = False
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    if "Rechts" in data:
        for m in re.finditer(r"Rechts\w*", data):
            ctx_start = max(0, m.start() - 50)
            ctx_end = min(len(data), m.end() + 50)
            snippet = data[ctx_start:ctx_end].replace("\n", " ")
            print(f'{f}: "{snippet}"')
            found = True

if not found:
    print("No 'Rechts' found in any XHTML files")

# Show first non-ASCII words from first chapter with German text
print("\n--- First 20 words with non-ASCII from content-10.xhtml ---")
data = z.read("OEBPS/Text/content-10.xhtml").decode("utf-8")
# Strip HTML tags to get text
text = re.sub(r"<[^>]+>", " ", data)
words = text.split()
count = 0
for w in words:
    has_nonascii = any(ord(c) > 127 for c in w)
    if has_nonascii:
        # Show hex bytes
        raw = w.encode("utf-8")
        hexstr = " ".join(f"{b:02X}" for b in raw)
        print(f'  "{w}" -> [{hexstr}]')
        count += 1
        if count >= 20:
            break
