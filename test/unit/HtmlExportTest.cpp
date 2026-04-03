#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/HtmlExporter.h"
#include "microreader/content/TextLayout.h"

using namespace microreader;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Output directory for generated HTML files
// ---------------------------------------------------------------------------

static fs::path output_dir() {
  fs::path dir = fs::path(TEST_FIXTURES_DIR).parent_path() / "output" / "html";
  fs::create_directories(dir);
  return dir;
}

// ---------------------------------------------------------------------------
// Font that approximates NotoSans 28px on the real 480x800 / 3.8" display (~245 DPI)
// ---------------------------------------------------------------------------

static FixedFont export_font(12, 28);

// ---------------------------------------------------------------------------
// Helper: find book path by searching known directories
// ---------------------------------------------------------------------------

static fs::path find_book(const char* relative_path) {
  fs::path workspace = fs::path(TEST_FIXTURES_DIR).parent_path().parent_path().parent_path();
  fs::path full = workspace / relative_path;
  if (fs::exists(full))
    return full;
  return {};
}

// ---------------------------------------------------------------------------
// Helper: sanitize filename
// ---------------------------------------------------------------------------

static std::string sanitize(const std::string& name) {
  std::string out;
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')
      out += c;
    else if (c == ' ')
      out += '_';
  }
  return out;
}

// ---------------------------------------------------------------------------
// Export a single book to HTML
// ---------------------------------------------------------------------------

static bool export_book(const char* rel_path, const HtmlExportOptions& opts = {}) {
  fs::path book_path = find_book(rel_path);
  if (book_path.empty() || !fs::exists(book_path)) {
    printf("  SKIP: %s not found\n", rel_path);
    return false;
  }

  Book book;
  CssConfig css_config;
  css_config.glyph_width = export_font.glyph_width;
  css_config.content_width = opts.page_width - 2 * opts.padding;
  book.set_css_config(css_config);
  EpubError err = book.open(book_path.string().c_str());
  if (err != EpubError::Ok) {
    printf("  ERROR opening %s: %d\n", rel_path, static_cast<int>(err));
    return false;
  }

  std::string filename =
      sanitize(book.metadata().title.empty() ? fs::path(rel_path).stem().string() : book.metadata().title) + ".html";

  fs::path out_path = output_dir() / filename;

  bool ok = export_to_html(book, export_font, opts, out_path.string().c_str());
  if (ok) {
    auto size = fs::file_size(out_path);
    printf("  OK: %s (%zuKB) -> %s\n", book.metadata().title.c_str(), size / 1024,
           out_path.filename().string().c_str());
  }
  return ok;
}

// ===========================================================================
// 10 Gutenberg picks (diverse languages, images, eras) — full book export
// ===========================================================================

static const char* gutenberg_picks[] = {
    "microreader2/test/books/gutenberg/alice-illustrated.epub",    // EN, illustrated
    "microreader2/test/books/gutenberg/metamorphosis-kafka.epub",  // EN, short fiction
    "microreader2/test/books/gutenberg/faust-de.epub",             // DE, poetry/drama
    "microreader2/test/books/gutenberg/divina-commedia-it.epub",   // IT, poetry
    "microreader2/test/books/gutenberg/don-quixote-es.epub",       // ES, novel
    "microreader2/test/books/gutenberg/les-miserables-fr.epub",    // FR, large novel
    "microreader2/test/books/gutenberg/beowulf-old-english.epub",  // Old English
    "microreader2/test/books/gutenberg/moby-dick.epub",            // EN, classic
    "microreader2/test/books/gutenberg/elements-euclid.epub",      // EN, diagrams
    "microreader2/test/books/gutenberg/treasure-island.epub",      // EN, illustrated
};

TEST(HtmlExport, GutenbergPicks) {
  HtmlExportOptions opts;  // all chapters, all pages
  int ok = 0, skip = 0;
  for (const char* path : gutenberg_picks) {
    if (export_book(path, opts))
      ++ok;
    else
      ++skip;
  }
  printf("  Gutenberg: %d exported, %d skipped\n", ok, skip);
  EXPECT_GT(ok, 0);
}

// ===========================================================================
// Regression test EPUB — comprehensive feature / edge-case coverage
// ===========================================================================

TEST(HtmlExport, RegressionTestEpub) {
  fs::path epub_path = fs::path(TEST_FIXTURES_DIR) / "regression_test.epub";
  if (!fs::exists(epub_path)) {
    printf("  SKIP: regression_test.epub not found (run generate_regression_epub.py)\n");
    GTEST_SKIP();
  }

  Book book;
  CssConfig css_config;
  HtmlExportOptions opts;
  css_config.glyph_width = export_font.glyph_width;
  css_config.content_width = opts.page_width - 2 * opts.padding;
  book.set_css_config(css_config);
  EpubError err = book.open(epub_path.string().c_str());
  ASSERT_EQ(err, EpubError::Ok) << "Failed to open regression_test.epub";

  fs::path out_path = output_dir() / "Regression_Test_Suite.html";
  bool ok = export_to_html(book, export_font, opts, out_path.string().c_str());
  ASSERT_TRUE(ok) << "Failed to export regression_test.epub";

  auto size = fs::file_size(out_path);
  printf("  Regression: %zuKB -> %s\n", size / 1024, out_path.filename().string().c_str());

  // Basic sanity: exported file should be non-trivial
  EXPECT_GT(size, 10000u);

  // Verify we got all 31 chapters
  std::ifstream file(out_path);
  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  int chapter_count = 0;
  std::string::size_type pos = 0;
  while ((pos = content.find("class=\"ch-title\"", pos)) != std::string::npos) {
    ++chapter_count;
    ++pos;
  }
  EXPECT_EQ(chapter_count, 40) << "Expected 40 chapters in regression test output";

  // Verify hidden text is NOT in the output
  EXPECT_EQ(content.find("THIS SHOULD NOT APPEAR"), std::string::npos)
      << "Hidden element content should not appear in output";
}

// ===========================================================================
// All own books — full book export with all chapters and pages
// ===========================================================================

TEST(HtmlExport, AllOwnBooks) {
  fs::path workspace = fs::path(TEST_FIXTURES_DIR).parent_path().parent_path().parent_path();

  // Own book directories (not gutenberg)
  std::vector<std::string> own_dirs = {
      "microreader2/test/books/other",
      "microreader/resources/books",
      "TrustyReader/sd",
  };

  HtmlExportOptions opts;  // all chapters, all pages

  int exported = 0, skipped = 0, failed = 0;

  for (const auto& dir_rel : own_dirs) {
    fs::path dir = workspace / dir_rel;
    if (!fs::exists(dir))
      continue;

    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() != ".epub")
        continue;

      Book book;
      EpubError err = book.open(entry.path().string().c_str());
      if (err != EpubError::Ok) {
        printf("  SKIP (open error): %s\n", entry.path().filename().string().c_str());
        ++skipped;
        continue;
      }

      std::string filename =
          sanitize(book.metadata().title.empty() ? entry.path().stem().string() : book.metadata().title) + ".html";

      fs::path out_path = output_dir() / filename;

      if (export_to_html(book, export_font, opts, out_path.string().c_str())) {
        auto size = fs::file_size(out_path);
        printf("  OK: %s (%zuKB) -> %s\n", book.metadata().title.c_str(), size / 1024,
               out_path.filename().string().c_str());
        ++exported;
      } else {
        printf("  FAIL: %s\n", entry.path().filename().string().c_str());
        ++failed;
      }
    }
  }

  printf("\n  Own Books Export Summary:\n");
  printf("  Exported: %d, Skipped: %d, Failed: %d\n", exported, skipped, failed);
  printf("  Output: %s\n", output_dir().string().c_str());

  EXPECT_EQ(failed, 0);
  EXPECT_GT(exported, 0);
}

// ===========================================================================
// Verify TOC links match chapter anchors in generated HTML
// ===========================================================================

// Parse an HTML file and verify every href="#chN" has a matching id="chN"
static int verify_toc_links(const fs::path& html_path) {
  std::ifstream file(html_path);
  if (!file.is_open())
    return -1;

  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  // Collect all href="#chN" from the TOC
  std::set<std::string> link_targets;
  std::regex href_re(R"___(href="#(ch\d+)")___");
  for (std::sregex_iterator it(content.begin(), content.end(), href_re), end; it != end; ++it) {
    link_targets.insert((*it)[1].str());
  }

  // Collect all id="chN" from chapter divs
  std::set<std::string> anchors;
  std::regex id_re(R"___(id="(ch\d+)")___");
  for (std::sregex_iterator it(content.begin(), content.end(), id_re), end; it != end; ++it) {
    anchors.insert((*it)[1].str());
  }

  int broken = 0;
  for (const auto& target : link_targets) {
    if (anchors.find(target) == anchors.end()) {
      printf("    BROKEN LINK: #%s has no matching anchor\n", target.c_str());
      ++broken;
    }
  }
  return broken;
}

// Test that TOC links in the Book model map correctly to spine indices
TEST(HtmlExport, TocLinksMatchSpine) {
  fs::path workspace = fs::path(TEST_FIXTURES_DIR).parent_path().parent_path().parent_path();

  // Test with books that have TOCs — pick a variety
  std::vector<std::string> test_books = {
      "microreader2/test/books/gutenberg/alice-illustrated.epub", "microreader2/test/books/gutenberg/moby-dick.epub",
      "microreader2/test/books/gutenberg/les-miserables-fr.epub", "microreader2/test/books/gutenberg/faust-de.epub",
      "microreader2/test/books/gutenberg/treasure-island.epub",
  };

  int tested = 0, broken_books = 0;

  for (const auto& rel : test_books) {
    fs::path path = workspace / rel;
    if (!fs::exists(path))
      continue;

    Book book;
    if (book.open(path.string().c_str()) != EpubError::Ok)
      continue;

    const auto& toc = book.toc();
    const auto& spine = book.epub().spine();

    if (toc.entries.empty())
      continue;

    ++tested;

    // Verify: every TOC file_idx should appear in spine
    std::set<uint16_t> spine_file_idxs;
    for (const auto& si : spine) {
      spine_file_idxs.insert(si.file_idx);
    }

    int bad_entries = 0;
    for (size_t i = 0; i < toc.entries.size(); ++i) {
      if (spine_file_idxs.find(toc.entries[i].file_idx) == spine_file_idxs.end()) {
        printf("  %s: TOC[%zu] '%s' file_idx=%u NOT in spine\n", book.metadata().title.c_str(), i,
               toc.entries[i].label.c_str(), toc.entries[i].file_idx);
        ++bad_entries;
      }
    }

    // Also export and verify the HTML links
    std::string filename = "toc_test_" + sanitize(book.metadata().title) + ".html";
    fs::path out = output_dir() / filename;

    HtmlExportOptions opts;
    export_to_html(book, export_font, opts, out.string().c_str());

    int broken_links = verify_toc_links(out);
    if (broken_links > 0) {
      printf("  %s: %d broken TOC link(s) in HTML\n", book.metadata().title.c_str(), broken_links);
      ++broken_books;
    } else {
      printf("  OK: %s — %zu TOC entries, %zu spine items, 0 broken links\n", book.metadata().title.c_str(),
             toc.entries.size(), spine.size());
    }

    EXPECT_EQ(bad_entries, 0) << "TOC entries reference files not in spine: " << book.metadata().title;
    EXPECT_EQ(broken_links, 0) << "Broken TOC links in HTML: " << book.metadata().title;
  }

  EXPECT_GT(tested, 0) << "No books with TOCs found";
  EXPECT_EQ(broken_books, 0);
}

// Test TOC mapping at the data level — spine index vs zip entry index
TEST(HtmlExport, TocToSpineMapping) {
  fs::path workspace = fs::path(TEST_FIXTURES_DIR).parent_path().parent_path().parent_path();

  // Scan all available books for TOC integrity
  std::vector<fs::path> book_dirs;
  for (const char* dir : {"microreader2/test/books/gutenberg", "microreader2/test/books/other",
                          "microreader/resources/books", "TrustyReader/sd"}) {
    fs::path d = workspace / dir;
    if (fs::exists(d))
      book_dirs.push_back(d);
  }

  int tested = 0, failed = 0;

  for (const auto& dir : book_dirs) {
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() != ".epub")
        continue;

      Book book;
      if (book.open(entry.path().string().c_str()) != EpubError::Ok)
        continue;

      const auto& toc = book.toc();
      const auto& spine = book.epub().spine();
      if (toc.entries.empty())
        continue;

      ++tested;

      // Build the same zip→spine map that HtmlExporter uses
      std::map<uint16_t, size_t> zip_to_spine;
      for (size_t si = 0; si < spine.size(); ++si) {
        zip_to_spine.emplace(spine[si].file_idx, si);
      }

      bool ok = true;
      for (size_t i = 0; i < toc.entries.size(); ++i) {
        auto it = zip_to_spine.find(toc.entries[i].file_idx);
        if (it == zip_to_spine.end()) {
          printf("  FAIL: %s — TOC[%zu] '%s' file_idx=%u not in spine\n", entry.path().filename().string().c_str(), i,
                 toc.entries[i].label.c_str(), toc.entries[i].file_idx);
          ok = false;
        }
      }

      if (!ok)
        ++failed;
    }
  }

  printf("  TOC→spine mapping: %d books tested, %d with unmapped entries\n", tested, failed);
  EXPECT_GT(tested, 0);
  EXPECT_EQ(failed, 0);
}
