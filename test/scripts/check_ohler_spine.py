#!/usr/bin/env python3
"""Check what XHTML files the last spine items of ohler.epub map to."""
import os, re, zipfile

epub_path = os.path.join(
    os.path.dirname(__file__), "..", "..", "sd", "books", "ohler.epub"
)
z = zipfile.ZipFile(epub_path)

container = z.read("META-INF/container.xml").decode()
opf_path = re.search(r'full-path="([^"]+)"', container).group(1)
opf = z.read(opf_path).decode()

manifest = {}
# Match <item> elements with id and href in any order
for m in re.finditer(r"<item\b([^>]+)/>", opf):
    attrs = m.group(1)
    id_m = re.search(r'id="([^"]+)"', attrs)
    href_m = re.search(r'href="([^"]+)"', attrs)
    if id_m and href_m:
        manifest[id_m.group(1)] = href_m.group(1)

spine = []
for m in re.finditer(r'<itemref\s+[^>]*idref="([^"]+)"', opf):
    spine.append(m.group(1))

opf_dir = os.path.dirname(opf_path)
print(f"Total spine items: {len(spine)}")

# Show size distribution of all unique XHTML files
sizes = {}
for i, sid in enumerate(spine):
    href = manifest.get(sid, "???")
    full = (opf_dir + "/" + href) if opf_dir else href
    try:
        size = z.getinfo(full).file_size
    except:
        size = -1
    if full not in sizes:
        sizes[full] = (size, [])
    sizes[full][1].append(i)

print(f"\nUnique XHTML files: {len(sizes)}")
for path, (size, indices) in sorted(sizes.items(), key=lambda x: -x[1][0]):
    print(
        f"  {size:>8} bytes  {path}  (spine indices: {indices[0]}..{indices[-1]}, count={len(indices)})"
    )

print(f"\nLast 5 spine items:")
for i in range(max(0, len(spine) - 5), len(spine)):
    href = manifest.get(spine[i], "???")
    full = (opf_dir + "/" + href) if opf_dir else href
    try:
        size = z.getinfo(full).file_size
    except:
        size = -1
    print(f"  spine[{i}] id={spine[i]} href={href} size={size}")

z.close()
