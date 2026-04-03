#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/MrbConverter.h"

namespace fs = std::filesystem;
using namespace microreader;

// ---------------------------------------------------------------------------
// Helpers (same discovery logic as the test suite)
// ---------------------------------------------------------------------------

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

static std::vector<std::string> discover_epubs(const std::string& dir) {
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

static std::vector<std::string> get_all_test_books() {
  std::string root = workspace_root();
  std::vector<std::string> all;
  for (auto& dir : {root + "/microreader2/test/books/gutenberg", root + "/microreader2/test/books/other"}) {
    auto books = discover_epubs(dir);
    all.insert(all.end(), books.begin(), books.end());
  }
  return all;
}

// Cache the book list so discovery only happens once.
static const std::vector<std::string>& all_books() {
  static auto books = get_all_test_books();
  return books;
}

// Find a book by partial filename match.
static const std::string* find_book(const char* needle) {
  for (const auto& path : all_books()) {
    if (fs::path(path).filename().string().find(needle) != std::string::npos)
      return &path;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Per-book benchmark: conversion only (excludes Book::open overhead)
// ---------------------------------------------------------------------------

static void BM_ConvertBook(benchmark::State& state, const std::string& epub_path) {
  auto filename = fs::path(epub_path).filename().string();
  auto mrb_path = (fs::temp_directory_path() / "bench.mrb").string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok) {
    state.SkipWithError("Cannot open book");
    return;
  }

  for (auto _ : state) {
    bool ok = convert_epub_to_mrb_streaming(book, mrb_path.c_str());
    benchmark::DoNotOptimize(ok);
  }

  auto mrb_size = fs::file_size(mrb_path);
  state.SetBytesProcessed(static_cast<int64_t>(mrb_size) * state.iterations());
  state.counters["chapters"] = static_cast<double>(book.chapter_count());
  state.counters["mrb_MB"] = mrb_size / 1048576.0;

  std::remove(mrb_path.c_str());
  std::remove((mrb_path + ".idx").c_str());
}

// ---------------------------------------------------------------------------
// Per-book benchmark: Book::open only
// ---------------------------------------------------------------------------

static void BM_OpenBook(benchmark::State& state, const std::string& epub_path) {
  for (auto _ : state) {
    Book book;
    auto err = book.open(epub_path.c_str());
    benchmark::DoNotOptimize(err);
  }
}

// ---------------------------------------------------------------------------
// Aggregate: convert ALL books end-to-end (open + convert)
// ---------------------------------------------------------------------------

static void BM_ConvertAllBooks(benchmark::State& state) {
  auto mrb_path = (fs::temp_directory_path() / "bench_all.mrb").string();
  const auto& books = all_books();

  for (auto _ : state) {
    int converted = 0;
    for (const auto& epub_path : books) {
      Book book;
      if (book.open(epub_path.c_str()) != EpubError::Ok)
        continue;
      if (convert_epub_to_mrb_streaming(book, mrb_path.c_str()))
        ++converted;
    }
    benchmark::DoNotOptimize(converted);
  }

  state.counters["books"] = static_cast<double>(books.size());

  std::remove(mrb_path.c_str());
  std::remove((mrb_path + ".idx").c_str());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// Key representative books at different sizes.
struct NamedBook {
  const char* label;
  const char* needle;
};

static const NamedBook kKeyBooks[] = {
    {"Shakespeare",     "complete-shakespeare"},
    {"Bible",           "bible_luther"        },
    {"WarAndPeace",     "war-and-peace"       },
    {"LesMiserables",   "les-miserables"      },
    {"AliceWonderland", "alice"               },
    {"Metamorphosis",   "metamorphosis"       },
};

static void RegisterBookBenchmarks() {
  for (const auto& [label, needle] : kKeyBooks) {
    const std::string* path = find_book(needle);
    if (!path)
      continue;
    std::string convert_name = std::string("Convert/") + label;
    std::string open_name = std::string("Open/") + label;
    benchmark::RegisterBenchmark(convert_name, BM_ConvertBook, *path)->Unit(benchmark::kMillisecond)->Iterations(3);
    benchmark::RegisterBenchmark(open_name, BM_OpenBook, *path)->Unit(benchmark::kMillisecond)->Iterations(5);
  }
}

// Register per-book benchmarks for every discovered epub.
static void RegisterAllIndividualBenchmarks() {
  for (const auto& path : all_books()) {
    auto stem = fs::path(path).stem().string();
    // Sanitize for benchmark name
    for (auto& c : stem)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
        c = '_';
    std::string name = std::string("ConvertAll/") + stem;
    benchmark::RegisterBenchmark(name, BM_ConvertBook, path)->Unit(benchmark::kMillisecond)->Iterations(1);
  }
}

int main(int argc, char** argv) {
  RegisterBookBenchmarks();
  RegisterAllIndividualBenchmarks();
  benchmark::RegisterBenchmark("ConvertAllBooks", BM_ConvertAllBooks)->Unit(benchmark::kMillisecond)->Iterations(1);

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
