#include "MrbWriter.h"

#include <cstring>

namespace microreader {

// ---------------------------------------------------------------------------
// BufferedFileWriter
// ---------------------------------------------------------------------------

bool BufferedFileWriter::open(const char* path) {
  close();
  f_ = fopen(path, "wb");
  if (!f_)
    return false;
  // Keep a modest stdio buffer for the underlying fwrite calls.
  setvbuf(f_, nullptr, _IOFBF, 4096);
  pos_ = 0;
  used_ = 0;
  return true;
}

void BufferedFileWriter::close() {
  if (f_) {
    flush();
    fclose(f_);
    f_ = nullptr;
  }
  pos_ = 0;
  used_ = 0;
}

bool BufferedFileWriter::flush() {
  if (used_ > 0) {
    if (fwrite(buf_, 1, used_, f_) != used_)
      return false;
    used_ = 0;
  }
  return true;
}

bool BufferedFileWriter::write(const void* data, size_t size) {
  const uint8_t* src = static_cast<const uint8_t*>(data);
  pos_ += static_cast<uint32_t>(size);
  // Fast path: fits in remaining buffer space.
  if (used_ + size <= kBufSize) {
    std::memcpy(buf_ + used_, src, size);
    used_ += size;
    return true;
  }
  // Flush current buffer.
  if (!flush())
    return false;
  // Large write: bypass buffer entirely.
  if (size >= kBufSize)
    return fwrite(src, 1, size, f_) == size;
  // Small write after flush: start fresh buffer.
  std::memcpy(buf_, src, size);
  used_ = size;
  return true;
}

bool BufferedFileWriter::seek(uint32_t offset) {
  if (!flush())
    return false;
  if (fseek(f_, static_cast<long>(offset), SEEK_SET) != 0)
    return false;
  pos_ = offset;
  return true;
}

// ---------------------------------------------------------------------------
// MrbWriter
// ---------------------------------------------------------------------------

bool MrbWriter::open(const char* path) {
  close();
  if (!bw_.open(path))
    return false;

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
  bw_.close();
  paragraph_count_ = 0;
  chapters_.clear();
  images_.clear();
  pending_para_.clear();
  serialize_buf_.clear();
  in_chapter_ = false;
  prev_para_offset_ = 0;
  pending_para_offset_ = 0;
  chapter_first_offset_ = 0;
  chapter_para_count_ = 0;
}

void MrbWriter::begin_chapter() {
  prev_para_offset_ = 0;
  chapter_first_offset_ = 0;
  chapter_para_count_ = 0;
  pending_para_.clear();
  pending_para_offset_ = 0;
  in_chapter_ = true;
}

void MrbWriter::end_chapter() {
  if (!in_chapter_)
    return;

  // Flush the last buffered paragraph with next_offset = 0 (end of chain).
  if (!pending_para_.empty())
    flush_pending(0);

  MrbChapterEntry entry{};
  entry.first_para_offset = chapter_first_offset_;
  entry.last_para_offset = prev_para_offset_;  // last written paragraph
  entry.paragraph_count = chapter_para_count_;
  chapters_.push_back(entry);
  in_chapter_ = false;
}

bool MrbWriter::flush_pending(uint32_t next_offset) {
  if (pending_para_.empty())
    return true;
  // Patch next_offset at bytes [4..7] in the buffered paragraph.
  mrb_write_u32(pending_para_.data() + 4, next_offset);
  if (!write_bytes(pending_para_.data(), pending_para_.size()))
    return false;
  pending_para_.clear();
  return true;
}

bool MrbWriter::stage_paragraph(const Paragraph& para) {
  if (!bw_.is_open())
    return false;

  // The file offset where this paragraph will land.
  // bw_.tell() gives the current disk position; add the pending paragraph size
  // because it hasn't been flushed yet.
  uint32_t this_offset = bw_.tell() + static_cast<uint32_t>(pending_para_.size());

  // If there is a previously staged paragraph, flush it now that we know
  // next_offset (= this_offset).
  if (!pending_para_.empty()) {
    if (!flush_pending(this_offset))
      return false;
  }

  // Track first paragraph in chapter.
  if (chapter_para_count_ == 0)
    chapter_first_offset_ = this_offset;

  // Build the complete paragraph bytes into pending_para_.
  pending_para_.clear();

  // Link header: [prev_offset(4)] [next_offset(4)]  — next patched later.
  uint8_t link[8];
  mrb_write_u32(link, prev_para_offset_);
  mrb_write_u32(link + 4, 0);  // next = 0 placeholder
  pending_para_.insert(pending_para_.end(), link, link + 8);

  // Paragraph body.
  switch (para.type) {
    case ParagraphType::Text: {
      uint16_t spacing = para.spacing_before.value_or(kMrbSpacingDefault);
      serialize_text(para.text, spacing);
      uint8_t hdr[5];
      hdr[0] = kMrbParaText;
      mrb_write_u32(hdr + 1, static_cast<uint32_t>(serialize_buf_.size()));
      pending_para_.insert(pending_para_.end(), hdr, hdr + 5);
      if (!serialize_buf_.empty())
        pending_para_.insert(pending_para_.end(), serialize_buf_.begin(), serialize_buf_.end());
      break;
    }
    case ParagraphType::Image: {
      uint8_t buf[9];
      buf[0] = kMrbParaImage;
      mrb_write_u32(buf + 1, 4);
      mrb_write_u16(buf + 5, para.image.key);
      mrb_write_u16(buf + 7, para.spacing_before.value_or(kMrbSpacingDefault));
      pending_para_.insert(pending_para_.end(), buf, buf + 9);
      break;
    }
    case ParagraphType::Hr: {
      uint8_t buf[7];
      buf[0] = kMrbParaHr;
      mrb_write_u32(buf + 1, 2);
      mrb_write_u16(buf + 5, para.spacing_before.value_or(kMrbSpacingDefault));
      pending_para_.insert(pending_para_.end(), buf, buf + 7);
      break;
    }
    case ParagraphType::PageBreak: {
      uint8_t buf[5];
      buf[0] = kMrbParaPageBreak;
      mrb_write_u32(buf + 1, 0);
      pending_para_.insert(pending_para_.end(), buf, buf + 5);
      break;
    }
  }

  pending_para_offset_ = this_offset;
  prev_para_offset_ = this_offset;
  ++chapter_para_count_;
  ++paragraph_count_;
  return true;
}

bool MrbWriter::write_paragraph(const Paragraph& para) {
  return stage_paragraph(para);
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

void MrbWriter::update_image_size(uint16_t idx, uint16_t width, uint16_t height) {
  if (idx < images_.size()) {
    images_[idx].width = width;
    images_[idx].height = height;
  }
}

bool MrbWriter::finish(const EpubMetadata& meta, const TableOfContents& toc) {
  if (!bw_.is_open())
    return false;

  // Close any open chapter.
  if (in_chapter_)
    end_chapter();

  // --- Write chapter table (16 bytes each) ---
  uint32_t chapter_offset = bw_.tell();
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
  uint32_t image_offset = bw_.tell();
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
  uint32_t meta_offset = bw_.tell();
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

  bw_.seek(0);
  if (!write_bytes(&hdr, sizeof(hdr)))
    return false;
  bw_.close();

  return true;
}

// ---------------------------------------------------------------------------
// Paragraph serialization
// ---------------------------------------------------------------------------

void MrbWriter::serialize_text(const TextParagraph& text, uint16_t spacing) {
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

  serialize_buf_.resize(total);
  uint8_t* p = serialize_buf_.data();

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
    mrb_write_u16(p, text.inline_image->attr_width);
    p += 2;
    mrb_write_u16(p, text.inline_image->attr_height);
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
}

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------

bool MrbWriter::write_bytes(const void* data, size_t size) {
  return bw_.write(data, size);
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
