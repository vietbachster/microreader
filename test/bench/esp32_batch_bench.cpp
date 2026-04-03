// Batch conversion benchmark for ESP32.
//
// Converts all EPUB files on the SD card to MRB and logs per-book timing
// over serial. Designed to be pasted into app_main() after app.start()
// for one-off benchmark runs, then removed.
//
// Prerequisites:
//   #include <dirent.h>
//   #include <sys/stat.h>
//   #include "esp_timer.h"
//   #include "microreader/content/Book.h"
//   #include "microreader/content/MrbConverter.h"
//
// Usage:
//   1. Add the includes above to platforms/esp32/main.cpp
//   2. Paste the block below into app_main() after `app.start(logger, queue);`
//   3. Build, flash, and capture output with:
//        python tools/serial_capture.py --output bench.log
//   4. Remove the block and extra includes when done.
//
// Output format (one line per book):
//   I (<tick>) bench:   OK  open=<ms>  convert=<ms>  total=<ms>  chapters=<n>  mrb=<KB>KB
//   I (<tick>) bench: === DONE: <n> books, grand total=<ms>ms ===

// --- BENCHMARK: convert all EPUBs and log timing ---
{
  static const char* books_dir = "/sdcard/books";
  DIR* dir = opendir(books_dir);
  if (dir) {
    // Collect epub paths (max 32).
    static char paths[32][280];
    int nbooks = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && nbooks < 32) {
      size_t len = strlen(ent->d_name);
      if (len > 5 && len < 220 && strcmp(ent->d_name + len - 5, ".epub") == 0) {
        snprintf(paths[nbooks], sizeof(paths[0]), "%s/%s", books_dir, ent->d_name);
        ++nbooks;
      }
    }
    closedir(dir);

    ESP_LOGI("bench", "=== BATCH CONVERSION BENCHMARK: %d books ===", nbooks);
    long grand_total_ms = 0;

    for (int i = 0; i < nbooks; ++i) {
      // Build .mrb path and delete existing.
      char mrb_path[300];
      strncpy(mrb_path, paths[i], sizeof(mrb_path));
      char* dot = strrchr(mrb_path, '.');
      if (dot)
        strcpy(dot, ".mrb");
      remove(mrb_path);

      // Extract just the filename for logging.
      const char* fname = strrchr(paths[i], '/');
      fname = fname ? fname + 1 : paths[i];

      ESP_LOGI("bench", "[%d/%d] %s", i + 1, nbooks, fname);
      ESP_LOGI("bench", "  heap: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
               (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

      int64_t t0 = esp_timer_get_time();

      microreader::Book book;
      auto err = book.open(paths[i]);

      int64_t t1 = esp_timer_get_time();
      long open_ms = (long)((t1 - t0) / 1000);

      if (err != microreader::EpubError::Ok) {
        ESP_LOGE("bench", "  OPEN FAILED (err=%d) %ldms", (int)err, open_ms);
        continue;
      }

      bool ok = microreader::convert_epub_to_mrb_streaming(book, mrb_path);

      int64_t t2 = esp_timer_get_time();
      long conv_ms = (long)((t2 - t1) / 1000);
      long total_ms = (long)((t2 - t0) / 1000);
      grand_total_ms += total_ms;

      // Get MRB file size.
      struct stat st;
      long mrb_size = 0;
      if (stat(mrb_path, &st) == 0)
        mrb_size = (long)st.st_size;

      if (ok) {
        ESP_LOGI("bench", "  OK  open=%ldms  convert=%ldms  total=%ldms  chapters=%u  mrb=%ldKB", open_ms, conv_ms,
                 total_ms, (unsigned)book.chapter_count(), mrb_size / 1024);
      } else {
        ESP_LOGE("bench", "  CONVERT FAILED  open=%ldms  convert=%ldms  chapters=%u", open_ms, conv_ms,
                 (unsigned)book.chapter_count());
      }

      // Explicitly close book to free heap before next iteration.
      book.close();
      ESP_LOGI("bench", "  heap after close: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
               (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    ESP_LOGI("bench", "=== DONE: %d books, grand total=%ldms ===", nbooks, grand_total_ms);
  }
}
// --- END BENCHMARK ---
