#include "MrbConverter.h"

#include "EpubParser.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

namespace microreader {

// Shared image-key remapping used by both converter paths.
namespace {
struct ImageMapping {
  uint16_t zip_key;
  uint16_t mrb_idx;
};

uint16_t get_or_add_image(MrbWriter& writer, std::vector<ImageMapping>& image_map, uint16_t zip_key, uint16_t w,
                          uint16_t h) {
  for (const auto& m : image_map) {
    if (m.zip_key == zip_key)
      return m.mrb_idx;
  }
  uint16_t idx = writer.add_image_ref(zip_key, w, h);
  image_map.push_back({zip_key, idx});
  return idx;
}

void remap_paragraph_images(Paragraph& para, MrbWriter& writer, std::vector<ImageMapping>& image_map) {
  if (para.type == ParagraphType::Image) {
    para.image.key = get_or_add_image(writer, image_map, para.image.key, para.image.width, para.image.height);
  }
  if (para.type == ParagraphType::Text && para.text.inline_image.has_value()) {
    auto& img = *para.text.inline_image;
    img.key = get_or_add_image(writer, image_map, img.key, img.width, img.height);
  }
}
}  // namespace

bool convert_epub_to_mrb(Book& book, const char* output_path) {
  MrbWriter writer;
  if (!writer.open(output_path))
    return false;

  std::vector<ImageMapping> image_map;

  for (size_t ci = 0; ci < book.chapter_count(); ++ci) {
    Chapter ch;
    EpubError err = book.load_chapter(ci, ch);
    if (err != EpubError::Ok)
      continue;

    writer.begin_chapter();

    for (auto& para : ch.paragraphs) {
      remap_paragraph_images(para, writer, image_map);
      if (!writer.write_paragraph(para))
        return false;
    }

    writer.end_chapter();
  }

  return writer.finish(book.metadata(), book.toc());
}

bool convert_epub_to_mrb_streaming(Book& book, const char* output_path) {
  MrbWriter writer;
  if (!writer.open(output_path))
    return false;

  std::vector<ImageMapping> image_map;

#ifdef ESP_PLATFORM
  int64_t total_start = esp_timer_get_time();
#endif

  // Context for the streaming paragraph sink.
  struct SinkCtx {
    MrbWriter* writer;
    std::vector<ImageMapping>* image_map;
    bool error;
  };
  SinkCtx ctx{&writer, &image_map, false};

  // Paragraph sink: remap images and write to MRB as each paragraph arrives.
  auto sink = [](void* raw_ctx, Paragraph&& para) {
    auto& c = *static_cast<SinkCtx*>(raw_ctx);
    if (c.error)
      return;

    remap_paragraph_images(para, *c.writer, *c.image_map);

    if (!c.writer->write_paragraph(para))
      c.error = true;
  };

  for (size_t ci = 0; ci < book.chapter_count(); ++ci) {
    writer.begin_chapter();

#ifdef ESP_PLATFORM
    int64_t ch_start = esp_timer_get_time();
#endif

    book.load_chapter_streaming(ci, sink, &ctx);
    if (ctx.error)
      return false;

    writer.end_chapter();

#ifdef ESP_PLATFORM
    long ch_ms = (long)((esp_timer_get_time() - ch_start) / 1000);
    ESP_LOGI("mrb", "ch %u/%u  %ldms  free=%lu largest=%lu", (unsigned)ci, (unsigned)book.chapter_count(), ch_ms,
             (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
  }

  bool ok = writer.finish(book.metadata(), book.toc());

#ifdef ESP_PLATFORM
  long total_ms = (long)((esp_timer_get_time() - total_start) / 1000);
  ESP_LOGI("mrb", "TOTAL conversion: %ldms (%u chapters)", total_ms, (unsigned)book.chapter_count());
#endif

  return ok;
}

}  // namespace microreader
