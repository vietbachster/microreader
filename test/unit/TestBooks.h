#pragma once
// TestBooks.h — single source of truth for the curated test book list
// and shared discovery helpers used across test files.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

namespace test_books {

namespace fs = std::filesystem;

inline std::string workspace_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  // fixtures = .../microreader2/test/fixtures → workspace = 3 levels up
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

inline std::vector<std::string> discover_epubs(const std::string& dir) {
  std::vector<std::string> result;
  if (!fs::exists(dir))
    return result;
  for (auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".epub")
        result.push_back(entry.path().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// Curated list of 15 representative EPUBs covering a range of sizes,
// languages, image content, CSS complexity, and previously-problematic books.
inline std::vector<std::string> get_curated_books() {
  static const char* kBooks[] = {
      "gutenberg/alice-wonderland.epub",       // small, simple
      "gutenberg/frankenstein.epub",           // medium classic
      "gutenberg/pride-prejudice.epub",        // large with images
      "gutenberg/moby-dick.epub",              // medium classic
      "gutenberg/dracula.epub",                // previously problematic (UTF-8 split)
      "gutenberg/adventures-tom-sawyer.epub",  // previously problematic (NBSP split)
      "gutenberg/complete-shakespeare.epub",   // large, many chapters, entity split
      "gutenberg/origin-species-darwin.epub",  // previously problematic
      "gutenberg/alice-illustrated.epub",      // images
      "gutenberg/war-and-peace.epub",          // large
      "gutenberg/heart-darkness.epub",         // short novella
      "gutenberg/metamorphosis-kafka.epub",    // short, simple
      "gutenberg/ulysses-joyce.epub",          // complex prose
      "gutenberg/buddenbrooks-de.epub",        // German
      "gutenberg/divina-commedia-it.epub",     // Italian / special chars
  };
  std::string base = workspace_root() + "/microreader2/test/books/";
  std::vector<std::string> all;
  for (auto& b : kBooks) {
    std::string path = base + b;
    if (fs::exists(path))
      all.push_back(path);
  }
  return all;
}

// All .epub files under test/books/ (gutenberg + other).
inline std::vector<std::string> get_all_books() {
  std::string root = workspace_root();
  std::vector<std::string> all;
  for (auto& dir : {root + "/microreader2/test/books/gutenberg", root + "/microreader2/test/books/other"}) {
    auto books = discover_epubs(dir);
    all.insert(all.end(), books.begin(), books.end());
  }
  return all;
}

// GTest name generator: stem with non-alphanumeric chars replaced by '_'.
inline std::string epub_test_name(const std::string& path) {
  auto name = fs::path(path).stem().string();
  for (auto& c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      c = '_';
  return name;
}

}  // namespace test_books
