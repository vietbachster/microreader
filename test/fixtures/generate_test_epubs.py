#!/usr/bin/env python3
"""Generate minimal test EPUB files for microreader2 unit tests.

Each EPUB is a valid ZIP with the required EPUB structure:
  mimetype (stored, first entry)
  META-INF/container.xml
  content.opf
  chapter(s)
  optional: stylesheet, images, TOC (toc.ncx)
"""

import io
import os
import struct
import zipfile
from pathlib import Path

OUT = Path(__file__).parent

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

MIMETYPE = "application/epub+zip"

CONTAINER_XML = """\
<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="{opf_path}" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>"""


def make_opf(*, title="Test Book", author="Test Author", language="en",
             manifest_items, spine_idrefs, cover_id=None, toc_id=None):
    manifest = "\n    ".join(
        f'<item id="{mid}" href="{href}" media-type="{mt}"/>'
        for mid, href, mt in manifest_items
    )
    spine_items = "\n    ".join(
        f'<itemref idref="{idref}"/>' for idref in spine_idrefs
    )
    cover_meta = ""
    if cover_id:
        cover_meta = f'\n    <meta name="cover" content="{cover_id}"/>'
    toc_attr = f' toc="{toc_id}"' if toc_id else ""
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>{title}</dc:title>
    <dc:creator>{author}</dc:creator>
    <dc:language>{language}</dc:language>
    <dc:identifier id="uid">test-{title.lower().replace(' ', '-')}</dc:identifier>{cover_meta}
  </metadata>
  <manifest>
    {manifest}
  </manifest>
  <spine{toc_attr}>
    {spine_items}
  </spine>
</package>"""


def make_xhtml(title, body_html):
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{title}</title></head>
<body>
{body_html}
</body>
</html>"""


def make_ncx(nav_points):
    """nav_points: list of (label, content_src)"""
    points = ""
    for i, (label, src) in enumerate(nav_points, 1):
        points += f"""
    <navPoint id="np{i}" playOrder="{i}">
      <navLabel><text>{label}</text></navLabel>
      <content src="{src}"/>
    </navPoint>"""
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head/>
  <docTitle><text>Test</text></docTitle>
  <navMap>{points}
  </navMap>
</ncx>"""


def write_epub(name, files, opf_path="OEBPS/content.opf"):
    """Write a valid EPUB (ZIP) file.
    files: list of (arcname, data_bytes, compress) tuples.
    mimetype is always added first as stored.
    """
    path = OUT / name
    with zipfile.ZipFile(path, "w") as zf:
        # mimetype must be first, stored, no extra field
        info = zipfile.ZipInfo("mimetype")
        info.compress_type = zipfile.ZIP_STORED
        zf.writestr(info, MIMETYPE)

        for arcname, data, compress in files:
            info = zipfile.ZipInfo(arcname)
            info.compress_type = (
                zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
            )
            zf.writestr(info, data if isinstance(data, bytes) else data.encode("utf-8"))

    print(f"  wrote {path}  ({path.stat().st_size} bytes)")


def enc(s):
    return s.encode("utf-8") if isinstance(s, str) else s


# ---------------------------------------------------------------------------
# 1. basic.epub — single chapter, plain text only
# ---------------------------------------------------------------------------
def gen_basic():
    opf_path = "OEBPS/content.opf"
    ch1 = make_xhtml("Chapter 1", "<h1>Chapter One</h1>\n<p>Hello, world!</p>\n<p>Second paragraph.</p>")
    opf = make_opf(
        title="Basic Test",
        manifest_items=[("ch1", "chapter1.xhtml", "application/xhtml+xml")],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/chapter1.xhtml", ch1, True),
    ]
    write_epub("basic.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 2. multi_chapter.epub — 3 chapters with TOC
# ---------------------------------------------------------------------------
def gen_multi_chapter():
    opf_path = "OEBPS/content.opf"
    chapters = []
    manifest = []
    spine = []
    nav_points = []
    for i in range(1, 4):
        cid = f"ch{i}"
        fname = f"chapter{i}.xhtml"
        body = f"<h1>Chapter {i}</h1>\n<p>Content of chapter {i}.</p>"
        if i == 2:
            body += "\n<p>This chapter has <b>bold</b> and <i>italic</i> text.</p>"
        chapters.append(("OEBPS/" + fname, make_xhtml(f"Chapter {i}", body), True))
        manifest.append((cid, fname, "application/xhtml+xml"))
        spine.append(cid)
        nav_points.append((f"Chapter {i}", fname))

    manifest.append(("ncx", "toc.ncx", "application/x-dtbncx+xml"))
    ncx = make_ncx(nav_points)
    opf = make_opf(
        title="Multi Chapter",
        manifest_items=manifest,
        spine_idrefs=spine,
        toc_id="ncx",
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/toc.ncx", ncx, True),
    ] + chapters
    write_epub("multi_chapter.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 3. with_css.epub — inline + external CSS
# ---------------------------------------------------------------------------
def gen_with_css():
    opf_path = "OEBPS/content.opf"
    css = "body { margin: 1em; } h1 { text-align: center; } .indent { text-indent: 2em; } .bold { font-weight: bold; }"
    ch1 = make_xhtml("Styled", """\
<h1>Styled Chapter</h1>
<p class="indent">This paragraph has text-indent.</p>
<p class="bold">This paragraph is bold via CSS class.</p>
<p style="font-style: italic;">This paragraph is italic via inline style.</p>""")
    opf = make_opf(
        title="CSS Test",
        manifest_items=[
            ("ch1", "chapter1.xhtml", "application/xhtml+xml"),
            ("css1", "style.css", "text/css"),
        ],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/style.css", css, True),
        ("OEBPS/chapter1.xhtml", ch1, True),
    ]
    write_epub("with_css.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 4. with_images.epub — chapter with JPEG and PNG image references
# ---------------------------------------------------------------------------
def gen_with_images():
    opf_path = "OEBPS/content.opf"
    # Generate a proper 1x1 white JPEG using PIL
    from PIL import Image
    img = Image.new("L", (1, 1), 255)  # 1x1 grayscale white
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=100)
    jpeg_data = buf.getvalue()

    # Minimal valid PNG (1x1 red pixel)
    import zlib
    def make_png_1x1(r, g, b):
        def chunk(ctype, data):
            c = ctype + data
            crc = struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
            return struct.pack(">I", len(data)) + c + crc
        sig = b"\x89PNG\r\n\x1a\n"
        ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)  # 1x1, 8bit RGB
        raw = b"\x00" + bytes([r, g, b])
        idat = zlib.compress(raw)
        return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")
    png_data = make_png_1x1(255, 0, 0)

    ch1 = make_xhtml("Images", """\
<h1>Chapter with Images</h1>
<p>A JPEG image:</p>
<img src="images/test.jpg" alt="test jpeg"/>
<p>A PNG image:</p>
<img src="images/test.png" alt="test png"/>
<p>End of chapter.</p>""")
    opf = make_opf(
        title="Image Test",
        manifest_items=[
            ("ch1", "chapter1.xhtml", "application/xhtml+xml"),
            ("img1", "images/test.jpg", "image/jpeg"),
            ("img2", "images/test.png", "image/png"),
        ],
        spine_idrefs=["ch1"],
        cover_id="img1",
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path="OEBPS/content.opf"), False),
        ("OEBPS/content.opf", opf, True),
        ("OEBPS/chapter1.xhtml", ch1, True),
        ("OEBPS/images/test.jpg", jpeg_data, False),  # Store images uncompressed
        ("OEBPS/images/test.png", png_data, False),
    ]
    write_epub("with_images.epub", files, "OEBPS/content.opf")


# ---------------------------------------------------------------------------
# 5. stored.epub — all entries stored (no compression), for simple ZIP test
# ---------------------------------------------------------------------------
def gen_stored():
    opf_path = "content.opf"
    ch1 = make_xhtml("Stored", "<p>All entries are stored without compression.</p>")
    opf = make_opf(
        title="Stored Test",
        manifest_items=[("ch1", "chapter1.xhtml", "application/xhtml+xml")],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, False),
        ("chapter1.xhtml", ch1, False),
    ]
    write_epub("stored.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 6. nested_dirs.epub — content in deeply nested directories
# ---------------------------------------------------------------------------
def gen_nested_dirs():
    opf_path = "OEBPS/sub/content.opf"
    ch1 = make_xhtml("Nested", "<p>Content in nested directory.</p>")
    opf = make_opf(
        title="Nested Dirs",
        manifest_items=[("ch1", "chapters/chapter1.xhtml", "application/xhtml+xml")],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/sub/chapters/chapter1.xhtml", ch1, True),
    ]
    write_epub("nested_dirs.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 7. special_chars.epub — Unicode title, author, and content
# ---------------------------------------------------------------------------
def gen_special_chars():
    opf_path = "OEBPS/content.opf"
    ch1 = make_xhtml("Sönderzeichen", """\
<h1>Ünïcödë Chäptér</h1>
<p>Héllo wörld! Ça va? 日本語テスト</p>
<p>Entities: &amp; &lt; &gt; &quot;</p>""")
    opf = make_opf(
        title="Ünïcödë Tëst",
        author="Ñoño Müller",
        manifest_items=[("ch1", "chapter1.xhtml", "application/xhtml+xml")],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/chapter1.xhtml", ch1, True),
    ]
    write_epub("special_chars.epub", files, opf_path)


# ---------------------------------------------------------------------------
# 8. large_chapter.epub — chapter with many paragraphs (stress test)
# ---------------------------------------------------------------------------
def gen_large_chapter():
    opf_path = "OEBPS/content.opf"
    paras = "\n".join(f"<p>Paragraph {i}: Lorem ipsum dolor sit amet, "
                      f"consectetur adipiscing elit. Sed do eiusmod tempor "
                      f"incididunt ut labore et dolore magna aliqua.</p>"
                      for i in range(1, 201))
    ch1 = make_xhtml("Large", f"<h1>Large Chapter</h1>\n{paras}")
    opf = make_opf(
        title="Large Chapter",
        manifest_items=[("ch1", "chapter1.xhtml", "application/xhtml+xml")],
        spine_idrefs=["ch1"],
    )
    files = [
        ("META-INF/container.xml", CONTAINER_XML.format(opf_path=opf_path), False),
        (opf_path, opf, True),
        ("OEBPS/chapter1.xhtml", ch1, True),
    ]
    write_epub("large_chapter.epub", files, opf_path)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    print("Generating test EPUBs...")
    gen_basic()
    gen_multi_chapter()
    gen_with_css()
    gen_with_images()
    gen_stored()
    gen_nested_dirs()
    gen_special_chars()
    gen_large_chapter()
    print("Done!")
