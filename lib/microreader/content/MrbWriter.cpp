#include "MrbWriter.h"

#include <cstring>

namespace microreader {

bool MrbWriter::open(const char* path) {
  close();
  f_ = fopen(path, "wb");
  if (!f_)
    return false;

  // Use larger I/O buffer for better write performance.
  setvbuf(f_, nullptr, _IOFBF, 8192);

  // Write placeholder header (will be fixed up in finish()).
  MrbHeader hdr{};
  std::memcpy(hdr.magic, kMrbMagic, 4);
  hdr.version = kMrbVersion;
  if (!write_bytes(&hdr, sizeof(hdr))) {
    close();
    return false;
  }
  return true;
}

void MrbWriter::close() {
  if (f_) {
    fclose(f_);
    f_ = nullptr;
  }
  paragraph_count_ = 0;
  chapters_.clear();
  images_.clear();
  in_chapter_ = false;
  prev_para_offset_ = 0;
  chapter_first_offset_ = 0;
  chapter_para_count_ = 0;
}

void MrbWriter::begin_chapter() {
  prev_para_offset_ = 0;
  chapter_first_offset_ = 0;
  chapter_para_count_ = 0;
  in_chapter_ = true;
}

void MrbWriter::end_chapter() {
  if (!in_chapter_)
    return;
  MrbChapterEntry entry{};
  entry.first_para_offset = chapter_first_offset_;
  entry.last_para_offset = prev_para_offset_;  // last written paragraph
  entry.paragraph_count = chapter_para_count_;
  chapters_.push_back(entry);
  in_chapter_ = false;
}

bool MrbWriter::write_paragraph(const Paragraph& para) {
  if (!f_)
    return false;

  uint32_t this_offset = static_cast<uint32_t>(ftell(f_));

  // Track first paragraph in chapter.
  if (chapter_para_count_ == 0)
    chapter_first_offset_ = this_offset;

  // Write linked-list header: [prev_offset(4)] [next_offset(4)]
  // next_offset is 0 for now — patched when the next paragraph is written.
  uint8_t link[8];
  mrb_write_u32(link, prev_para_offset_);
  mrb_write_u32(link + 4, 0);  // next = 0 (unknown yet)
  if (!write_bytes(link, 8))
    return false;

  // Patch the previous paragraph's next_offset to point to this one.
  if (prev_para_offset_ != 0) {
    long save_pos = ftell(f_);
    fseek(f_, static_cast<long>(prev_para_offset_ + 4), SEEK_SET);
    uint8_t next_buf[4];
    mrb_write_u32(next_buf, this_offset);
    if (!write_bytes(next_buf, 4))
      return false;
    fseek(f_, save_pos, SEEK_SET);
  }

  prev_para_offset_ = this_offset;
  ++chapter_para_count_;
  ++paragraph_count_;

  switch (para.type) {
    case ParagraphType::Text: {
      uint16_t spacing = para.spacing_before.value_or(kMrbSpacingDefault);
      auto body = serialize_text(para.text, spacing);
      uint8_t hdr[5];
      hdr[0] = kMrbParaText;
      mrb_write_u32(hdr + 1, static_cast<uint32_t>(body.size()));
      if (!write_bytes(hdr, 5))
        return false;
      if (!body.empty() && !write_bytes(body.data(), body.size()))
        return false;
      break;
    }
    case ParagraphType::Image: {
      uint8_t buf[9];
      buf[0] = kMrbParaImage;
      mrb_write_u32(buf + 1, 4);  // data_size = 4 bytes
      mrb_write_u16(buf + 5, para.image.key);
      mrb_write_u16(buf + 7, para.spacing_before.value_or(kMrbSpacingDefault));
      if (!write_bytes(buf, sizeof(buf)))
        return false;
      break;
    }
    case ParagraphType::Hr: {
      uint8_t buf[7];
      buf[0] = kMrbParaHr;
      mrb_write_u32(buf + 1, 2);  // data_size = 2 bytes
      mrb_write_u16(buf + 5, para.spacing_before.value_or(kMrbSpacingDefault));
      if (!write_bytes(buf, sizeof(buf)))
        return false;
      break;
    }
    case ParagraphType::PageBreak: {
      uint8_t buf[5];
      buf[0] = kMrbParaPageBreak;
      mrb_write_u32(buf + 1, 0);  // data_size = 0
      if (!write_bytes(buf, sizeof(buf)))
        return false;
      break;
    }
  }

  return true;
}

uint16_t MrbWriter::add_image_ref(uint16_t zip_entry_index, uint16_t width, uint16_t height) {
  uint16_t idx = static_cast<uint16_t>(images_.size());
  MrbImageRef ref{};
  ref.zip_entry_index = zip_entry_index;
  ref.width = width;
  ref.height = height;
  images_.push_back(ref);
  return idx;
}

bool MrbWriter::finish(const EpubMetadata& meta, const TableOfContents& toc) {
  if (!f_)
    return false;

  // Close any open chapter.
  if (in_chapter_)
    end_chapter();

  // --- Write chapter table (16 bytes each) ---
  uint32_t chapter_offset = static_cast<uint32_t>(ftell(f_));
  for (const auto& ch : chapters_) {
    uint8_t buf[16];
    mrb_write_u32(buf, ch.first_para_offset);
    mrb_write_u32(buf + 4, ch.last_para_offset);
    mrb_write_u16(buf + 8, ch.paragraph_count);
    mrb_write_u16(buf + 10, 0);
    mrb_write_u32(buf + 12, 0);
    if (!write_bytes(buf, 16))
      return false;
  }

  // --- Write image ref table ---
  uint32_t image_offset = static_cast<uint32_t>(ftell(f_));
  for (const auto& img : images_) {
    uint8_t buf[8];
    mrb_write_u16(buf, img.zip_entry_index);
    mrb_write_u16(buf + 2, img.width);
    mrb_write_u16(buf + 4, img.height);
    mrb_write_u16(buf + 6, 0);
    if (!write_bytes(buf, 8))
      return false;
  }

  // --- Write metadata blob ---
  uint32_t meta_offset = static_cast<uint32_t>(ftell(f_));
  write_string(meta.title);
  write_string(meta.author.value_or(""));
  write_string(meta.language.value_or(""));

  // --- Write TOC ---
  uint16_t toc_count = static_cast<uint16_t>(toc.entries.size());
  uint8_t toc_hdr[2];
  mrb_write_u16(toc_hdr, toc_count);
  write_bytes(toc_hdr, 2);
  for (const auto& entry : toc.entries) {
    write_string(entry.label);
    uint8_t fidx[2];
    mrb_write_u16(fidx, entry.file_idx);
    write_bytes(fidx, 2);
  }

  // --- Fix up header ---
  MrbHeader hdr{};
  std::memcpy(hdr.magic, kMrbMagic, 4);
  hdr.version = kMrbVersion;
  hdr.flags = 0;
  hdr.paragraph_count = paragraph_count_;
  hdr.chapter_count = static_cast<uint16_t>(chapters_.size());
  hdr.image_count = static_cast<uint16_t>(images_.size());
  hdr.reserved = 0;
  hdr.chapter_offset = chapter_offset;
  hdr.image_offset = image_offset;
  hdr.meta_offset = meta_offset;

  fseek(f_, 0, SEEK_SET);
  if (!write_bytes(&hdr, sizeof(hdr)))
    return false;
  fseek(f_, 0, SEEK_END);

  return true;
}

// ---------------------------------------------------------------------------
// Paragraph serialization
// ---------------------------------------------------------------------------

std::vector<uint8_t> MrbWriter::serialize_text(const TextParagraph& text, uint16_t spacing) {
  // Layout:
  //   alignment(1) + indent(2) + margin_left(2) + margin_right(2) +
  //   spacing_before(2) + line_height_pct(1) + inline_image_idx(2) +
  //   inline_image_w(2) + inline_image_h(2) +
  //   run_count(2) +
  //   per run: style(1) + size(1) + vertical_align(1) + flags(1) +
  //            margin_left(2) + margin_right(2) + text_len(4) + text[text_len]

  // Pre-compute total size to avoid repeated resizes.
  static constexpr size_t kHeaderSize = 1 + 2 + 2 + 2 + 2 + 1 + 2 + 2 + 2 + 2;  // 18 bytes
  static constexpr size_t kRunHeaderSize = 1 + 1 + 1 + 1 + 2 + 2 + 4;           // 12 bytes per run
  size_t total = kHeaderSize;
  for (const auto& run : text.runs)
    total += kRunHeaderSize + run.text.size();

  std::vector<uint8_t> buf(total);
  uint8_t* p = buf.data();

  // Header
  *p++ = text.alignment.has_value() ? static_cast<uint8_t>(*text.alignment) : kMrbAlignDefault;
  mrb_write_i16(p, text.indent.value_or(kMrbIndentNone));
  p += 2;
  mrb_write_u16(p, 0);
  p += 2;  // margin_left placeholder
  mrb_write_u16(p, 0);
  p += 2;  // margin_right placeholder
  mrb_write_u16(p, spacing);
  p += 2;
  *p++ = text.line_height_pct;

  // Inline image
  if (text.inline_image.has_value()) {
    mrb_write_u16(p, text.inline_image->key);
    p += 2;
    mrb_write_u16(p, text.inline_image->width);
    p += 2;
    mrb_write_u16(p, text.inline_image->height);
    p += 2;
  } else {
    mrb_write_u16(p, kMrbNoImage);
    p += 2;
    mrb_write_u16(p, 0);
    p += 2;
    mrb_write_u16(p, 0);
    p += 2;
  }

  // Run count
  mrb_write_u16(p, static_cast<uint16_t>(text.runs.size()));
  p += 2;

  // Runs
  for (const auto& run : text.runs) {
    *p++ = static_cast<uint8_t>(run.style);
    *p++ = static_cast<uint8_t>(run.size);
    *p++ = static_cast<uint8_t>(run.vertical_align);
    *p++ = run.breaking ? 0x01 : 0x00;
    mrb_write_u16(p, run.margin_left);
    p += 2;
    mrb_write_u16(p, run.margin_right);
    p += 2;
    uint32_t text_len = static_cast<uint32_t>(run.text.size());
    mrb_write_u32(p, text_len);
    p += 4;
    std::memcpy(p, run.text.data(), text_len);
    p += text_len;
  }

  return buf;
}

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------

bool MrbWriter::write_bytes(const void* data, size_t size) {
  return fwrite(data, 1, size, f_) == size;
}

bool MrbWriter::write_string(const std::string& s) {
  uint8_t len_buf[2];
  mrb_write_u16(len_buf, static_cast<uint16_t>(s.size()));
  if (!write_bytes(len_buf, 2))
    return false;
  if (!s.empty() && !write_bytes(s.data(), s.size()))
    return false;
  return true;
}

}  // namespace microreader
