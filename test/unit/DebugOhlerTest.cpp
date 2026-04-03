// Quick debug test for Ohler EPUB parsing diagnosis
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/XmlReader.h"
#include "microreader/content/ZipReader.h"

using namespace microreader;

static std::string workspace_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  std::string up2 = up1.substr(0, pos);
  pos = up2.rfind('/');
  if (pos == std::string::npos)
    pos = up2.rfind('\\');
  return up2.substr(0, pos);
}

TEST(DebugOhler, InspectZipEntries) {
  std::string path = workspace_root() + "/microreader/resources/books/ohler.epub";
  std::ifstream f(path);
  if (!f.good()) {
    GTEST_SKIP() << "Not found: " << path;
    return;
  }

  StdioZipFile zf;
  ASSERT_TRUE(zf.open(path.c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  printf("Total zip entries: %zu\n", zip.entry_count());

  // Show first 20 entries
  for (size_t i = 0; i < std::min(zip.entry_count(), size_t(20)); ++i) {
    printf("  [%zu] %s (%u bytes)\n", i, zip.entry(i).name.c_str(), zip.entry(i).uncompressed_size);
  }

  // Check specific entries
  auto* container = zip.find("META-INF/container.xml");
  printf("container.xml: %s\n", container ? "FOUND" : "MISSING");

  auto* opf = zip.find("OEBPS/content.opf");
  printf("content.opf: %s\n", opf ? "FOUND" : "MISSING");

  // Check a known XHTML entry
  auto* sample = zip.find("OEBPS/Text/content-10.xhtml");
  printf("OEBPS/Text/content-10.xhtml: %s\n", sample ? "FOUND" : "MISSING");

  auto* titlepage = zip.find("OEBPS/Text/titlepage.xhtml");
  printf("OEBPS/Text/titlepage.xhtml: %s\n", titlepage ? "FOUND" : "MISSING");

  // List all XHTML entries
  size_t xhtml_count = 0;
  for (size_t i = 0; i < zip.entry_count(); ++i) {
    if (zip.entry(i).name.find(".xhtml") != std::string::npos) {
      xhtml_count++;
    }
  }
  printf("Total XHTML entries: %zu\n", xhtml_count);
}

TEST(DebugOhler, ParseOPF) {
  std::string path = workspace_root() + "/microreader/resources/books/ohler.epub";
  std::ifstream f(path);
  if (!f.good()) {
    GTEST_SKIP() << "Not found: " << path;
    return;
  }

  StdioZipFile zf;
  ASSERT_TRUE(zf.open(path.c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  // Extract and parse the OPF manually
  auto* opf_entry = zip.find("OEBPS/content.opf");
  ASSERT_NE(opf_entry, nullptr);

  std::vector<uint8_t> opf_data;
  ASSERT_EQ(zip.extract(zf, *opf_entry, opf_data), ZipError::Ok);
  printf("OPF size: %zu bytes\n", opf_data.size());

  // Parse with XmlReader and count elements
  MemoryXmlInput input(opf_data.data(), opf_data.size());
  std::vector<uint8_t> buf(8192);  // use larger buffer
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf.data(), buf.size()), XmlError::Ok);

  size_t start_elements = 0;
  size_t end_elements = 0;
  size_t text_events = 0;
  bool found_manifest = false;
  bool found_spine = false;
  size_t manifest_items = 0;
  size_t spine_items = 0;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement) {
      start_elements++;
      if (ev.name == "manifest") {
        found_manifest = true;
        printf("Found <manifest> at element #%zu\n", start_elements);
      }
      if (ev.name == "item")
        manifest_items++;
      if (ev.name == "spine") {
        found_spine = true;
        printf("Found <spine> at element #%zu\n", start_elements);
      }
      if (ev.name == "itemref")
        spine_items++;
    } else if (ev.type == XmlEventType::EndElement) {
      end_elements++;
    } else if (ev.type == XmlEventType::Text) {
      text_events++;
    }
  }

  printf("Start elements: %zu\n", start_elements);
  printf("End elements: %zu\n", end_elements);
  printf("Text events: %zu\n", text_events);
  printf("Found manifest: %s, items: %zu\n", found_manifest ? "yes" : "no", manifest_items);
  printf("Found spine: %s, itemrefs: %zu\n", found_spine ? "yes" : "no", spine_items);
}

TEST(DebugOhler, HierarchicalParse) {
  std::string path = workspace_root() + "/microreader/resources/books/ohler.epub";
  std::ifstream f(path);
  if (!f.good()) {
    GTEST_SKIP() << "Not found: " << path;
    return;
  }

  StdioZipFile zf;
  ASSERT_TRUE(zf.open(path.c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* opf_entry = zip.find("OEBPS/content.opf");
  ASSERT_NE(opf_entry, nullptr);

  std::vector<uint8_t> opf_data;
  ASSERT_EQ(zip.extract(zf, *opf_entry, opf_data), ZipError::Ok);

  // Replicate parse_opf logic exactly
  MemoryXmlInput input(opf_data.data(), opf_data.size());
  uint8_t buf[8192];  // Try larger buffer
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf, sizeof(buf)), XmlError::Ok);

  size_t event_count = 0;
  size_t manifest_items = 0;
  size_t spine_items = 0;
  bool found_metadata = false;
  bool found_manifest = false;
  bool found_spine = false;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    event_count++;
    if (ev.type == XmlEventType::EndOfFile) {
      printf("EOF at event #%zu\n", event_count);
      break;
    }

    if (ev.type == XmlEventType::StartElement) {
      std::string name(ev.name.data, ev.name.length);

      if (name == "metadata") {
        found_metadata = true;
        printf("Found <metadata> at event #%zu, consuming subelements...\n", event_count);
        // Replicate parse_metadata: consume until </metadata>
        size_t sub_count = 0;
        XmlEvent sub;
        while (reader.next_event(sub) == XmlError::Ok) {
          sub_count++;
          if (sub.type == XmlEventType::EndOfFile) {
            printf("  UNEXPECTED EOF after %zu sub-events!\n", sub_count);
            break;
          }
          if (sub.type == XmlEventType::EndElement) {
            std::string ename(sub.name.data, sub.name.length);
            if (ename == "metadata") {
              printf("  </metadata> after %zu sub-events\n", sub_count);
              break;
            }
          }
        }
      } else if (name == "manifest") {
        found_manifest = true;
        printf("Found <manifest> at event #%zu, consuming items...\n", event_count);
        // Replicate parse_manifest: consume until </manifest>
        XmlEvent sub;
        while (reader.next_event(sub) == XmlError::Ok) {
          if (sub.type == XmlEventType::EndOfFile) {
            printf("  UNEXPECTED EOF during manifest!\n");
            break;
          }
          if (sub.type == XmlEventType::EndElement) {
            std::string ename(sub.name.data, sub.name.length);
            if (ename == "manifest") {
              printf("  </manifest> after %zu items\n", manifest_items);
              break;
            }
          }
          if (sub.type == XmlEventType::StartElement) {
            std::string sname(sub.name.data, sub.name.length);
            if (sname == "item")
              manifest_items++;
          }
        }
      } else if (name == "spine") {
        found_spine = true;
        printf("Found <spine> at event #%zu, consuming itemrefs...\n", event_count);
        XmlEvent sub;
        while (reader.next_event(sub) == XmlError::Ok) {
          if (sub.type == XmlEventType::EndOfFile) {
            printf("  UNEXPECTED EOF during spine!\n");
            break;
          }
          if (sub.type == XmlEventType::EndElement) {
            std::string ename(sub.name.data, sub.name.length);
            if (ename == "spine") {
              printf("  </spine> after %zu itemrefs\n", spine_items);
              break;
            }
          }
          if (sub.type == XmlEventType::StartElement) {
            std::string sname(sub.name.data, sub.name.length);
            if (sname == "itemref")
              spine_items++;
          }
        }
      }
    }
  }

  printf("Total main-loop events: %zu\n", event_count);
  printf("Found metadata: %s, manifest: %s (%zu items), spine: %s (%zu items)\n", found_metadata ? "yes" : "no",
         found_manifest ? "yes" : "no", manifest_items, found_spine ? "yes" : "no", spine_items);

  EXPECT_TRUE(found_manifest);
  EXPECT_TRUE(found_spine);
  EXPECT_GT(manifest_items, 0u);
  EXPECT_GT(spine_items, 0u);
}

TEST(DebugOhler, FullBookOpen) {
  std::string path = workspace_root() + "/microreader/resources/books/ohler.epub";
  std::ifstream f(path);
  if (!f.good()) {
    GTEST_SKIP() << "Not found: " << path;
    return;
  }

  Book book;
  auto err = book.open(path.c_str());
  printf("Book.open error: %d\n", (int)err);
  printf("Chapter count: %zu\n", book.chapter_count());
  printf("Title: %s\n", book.metadata().title.c_str());
  printf("Spine size: %zu\n", book.epub().spine().size());
  printf("Stylesheet rules: %zu\n", book.epub().stylesheet().rule_count());
  printf("TOC entries: %zu\n", book.toc().entries.size());

  EXPECT_EQ(err, EpubError::Ok);
  EXPECT_GT(book.chapter_count(), 0u);
}
