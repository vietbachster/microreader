"""Extract all unique characters from ohler.epub and check against font coverage."""

import zipfile
import html.parser
import collections
import unicodedata
import os

FONT_MIN = 0x20  # space
FONT_MAX = 0x7E  # tilde
EPUB_PATH = os.path.join(os.path.dirname(__file__), "..", "sd", "books", "ohler.epub")


class TextExtractor(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.text_parts = []
        self._skip = False

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self._skip = True

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self._skip = False

    def handle_data(self, data):
        if not self._skip:
            self.text_parts.append(data)

    def handle_entityref(self, name):
        import html

        char = html.unescape(f"&{name};")
        if not self._skip:
            self.text_parts.append(char)

    def handle_charref(self, name):
        import html

        char = html.unescape(f"&#{name};")
        if not self._skip:
            self.text_parts.append(char)


def main():
    char_counts = collections.Counter()

    with zipfile.ZipFile(EPUB_PATH) as zf:
        for name in zf.namelist():
            if (
                name.endswith((".xhtml", ".html", ".htm", ".xml"))
                and "META-INF" not in name
            ):
                data = zf.read(name).decode("utf-8", errors="replace")
                extractor = TextExtractor()
                extractor.feed(data)
                full_text = "".join(extractor.text_parts)
                for ch in full_text:
                    char_counts[ch] += 1

    # Separate into in-font and out-of-font
    in_font = {}
    out_font = {}
    for ch, count in sorted(char_counts.items(), key=lambda x: ord(x[0])):
        cp = ord(ch)
        if FONT_MIN <= cp <= FONT_MAX:
            in_font[ch] = count
        else:
            out_font[ch] = count

    print(f"=== Font coverage analysis for ohler.epub ===")
    print(f"Total unique characters: {len(char_counts)}")
    print(f"In font (0x20-0x7E):    {len(in_font)}")
    print(f"Out of font:            {len(out_font)}")
    print()

    print("=== Characters NOT in font (will render as '?') ===")
    print(f"{'Char':<6} {'U+':<8} {'Count':>6}  {'Name'}")
    print("-" * 60)
    for ch, count in sorted(out_font.items(), key=lambda x: ord(x[0])):
        cp = ord(ch)
        try:
            name = unicodedata.name(ch)
        except ValueError:
            name = "(unknown)"
        # Show repr for whitespace/control chars
        if (
            unicodedata.category(ch).startswith("C")
            or unicodedata.category(ch) == "Zs"
            or unicodedata.category(ch) == "Zl"
        ):
            display = repr(ch)
        else:
            display = ch
        print(f"{display:<6} U+{cp:04X}  {count:>6}  {name}")

    print()
    print("=== Summary by category ===")
    categories = collections.Counter()
    for ch in out_font:
        cat = unicodedata.category(ch)
        categories[cat] += out_font[ch]
    for cat, count in sorted(categories.items()):
        print(f"  {cat}: {count} occurrences")


if __name__ == "__main__":
    main()
