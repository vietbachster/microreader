import zipfile, re

z = zipfile.ZipFile(
    r"c:\Users\Patrick\Desktop\microreader\microreader2\sd\books\ohler.epub"
)
xhtml = [n for n in z.namelist() if n.endswith((".xhtml", ".html", ".htm"))]

# Check for soft hyphens (U+00AD = C2 AD in UTF-8),
# zero-width spaces (U+200B = E2 80 8B)
# zero-width non-joiner (U+200C = E2 80 8C)
# and other invisible characters
print("=== Checking for invisible Unicode characters ===")
total_soft_hyphens = 0
total_zwsp = 0
total_other = 0
files_with_shy = []

for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", "", data)

    # Soft hyphens
    shy_count = text.count("\u00ad")
    if shy_count > 0:
        total_soft_hyphens += shy_count
        files_with_shy.append((f, shy_count))

    # Zero-width space
    zwsp_count = text.count("\u200b")
    if zwsp_count > 0:
        total_zwsp += zwsp_count

    # Check for any non-printing chars in 0x80-0x9F range (C1 controls)
    for ch in text:
        cp = ord(ch)
        if 0x80 <= cp <= 0x9F:
            total_other += 1

print(f"Soft hyphens (U+00AD): {total_soft_hyphens} total")
print(f"Zero-width spaces (U+200B): {total_zwsp} total")
print(f"C1 control chars: {total_other} total")
print(f"Files with soft hyphens: {len(files_with_shy)}")
if files_with_shy:
    for f, c in files_with_shy[:5]:
        print(f"  {f}: {c} soft hyphens")

# Show some words with soft hyphens
print("\n=== Words with soft hyphens ===")
count = 0
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    words = text.split()
    for w in words:
        if "\u00ad" in w:
            # Show the word with shy marked
            display = w.replace("\u00ad", "|SHY|")
            raw = w.encode("utf-8")
            hexstr = " ".join(f"{b:02X}" for b in raw)
            print(f'  "{display}" -> [{hexstr}]')
            count += 1
            if count >= 15:
                break
    if count >= 15:
        break

# Check for non-breaking space (U+00A0 = C2 A0)
print("\n=== Non-breaking spaces ===")
total_nbsp = 0
for f in xhtml:
    data = z.read(f).decode("utf-8", errors="replace")
    text = re.sub(r"<[^>]+>", " ", data)
    total_nbsp += text.count("\u00a0")
print(f"Non-breaking spaces (U+00A0): {total_nbsp}")
