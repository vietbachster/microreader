import zipfile, re

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)
xhtml = [n for n in z.namelist() if n.endswith((".xhtml", ".html", ".htm"))]

# Broader search - case insensitive, partial matches
terms = ["Rechtsnachweis", "Rechtsnach", "Nachweis", "Nr.1", "Nr. 1", "Nr.\xa01"]
for term in terms:
    count = 0
    for f in xhtml:
        data = z.read(f).decode("utf-8", errors="replace")
        text = re.sub(r"<[^>]+>", " ", data)
        if term in text:
            # Get context
            idx = text.find(term)
            ctx = text[max(0, idx - 40) : idx + len(term) + 40].strip()
            ctx = re.sub(r"\s+", " ", ctx)
            print(f'  Found "{term}" in {f}: "...{ctx}..."')
            count += 1
            if count >= 3:
                break
    if count == 0:
        print(f'  "{term}" not found')

# Search for "Nr" preceded by special chars
print("\n=== Looking for Nr preceded by non-ASCII ===")
count = 0
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    for m in re.finditer(r"[^\x00-\x7F].{0,5}Nr\.", text):
        ctx_start = max(0, m.start() - 20)
        ctx_end = min(len(text), m.end() + 20)
        ctx = text[ctx_start:ctx_end].strip()
        raw = ctx.encode("utf-8")
        hexstr = " ".join(f"{b:02X}" for b in raw)
        print(f'  {f}: "{ctx}"')
        print(f"    Hex: {hexstr}")
        count += 1
        if count >= 5:
            break
    if count >= 5:
        break

# Search for "Nachweis" with any adjacent text
print("\n=== All 'nachweis' occurrences (case-insensitive) ===")
count = 0
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    for m in re.finditer(r"\S*[Nn]achweis\S*", text):
        word = m.group()
        ctx_start = max(0, m.start() - 30)
        ctx_end = min(len(text), m.end() + 30)
        ctx = text[ctx_start:ctx_end].strip()
        ctx = re.sub(r"\s+", " ", ctx)
        print(f'  {f}: "{ctx}"')
        count += 1
        if count >= 10:
            break
    if count >= 10:
        break

if count == 0:
    print("  Not found")
