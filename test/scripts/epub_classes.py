import zipfile, re, collections

z = zipfile.ZipFile(r"books/other/ohler.epub")
data = z.read("OEBPS/Text/content-14_split_001.xhtml").decode()
classes = re.findall(r'class="([^"]+)"', data)
c = collections.Counter(classes)
for cls, cnt in c.most_common(20):
    print(f"{cnt:5d}  {cls}")
