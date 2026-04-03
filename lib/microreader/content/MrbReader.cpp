#include "MrbReader.h"

#include <cstring>

namespace microreader {

bool MrbReader::open(const char* path) {
  close();
  f_ = fopen(path, "rb");
  if (!f_)
    return false;

  // Read header
  if (!read_bytes(&header_, sizeof(header_))) {
    close();
    return false;
  }
  if (std::memcmp(header_.magic, kMrbMagic, 4) != 0 || header_.version != kMrbVersion) {
    close();
    return false;
  }

  // Skip paragraph index — entries are read on demand via seek in
  // load_paragraph() to avoid allocating paragraph_count * 8 bytes of RAM.
  // (War and Peace has ~17 000 paragraphs → 136 KB just for the index.)

  // Read chapter table
  chapters_.resize(header_.chapter_count);
  if (header_.chapter_count > 0) {
    fseek(f_, static_cast<long>(header_.chapter_offset), SEEK_SET);
    for (uint16_t i = 0; i < header_.chapter_count; ++i) {
      uint8_t buf[8];
      if (!read_bytes(buf, 8)) {
        close();
        return false;
      }
      chapters_[i].first_paragraph = mrb_read_u32(buf);
      chapters_[i].paragraph_count = mrb_read_u16(buf + 4);
      chapters_[i].reserved = mrb_read_u16(buf + 6);
    }
  }

  // Read image ref table
  images_.resize(header_.image_count);
  if (header_.image_count > 0) {
    fseek(f_, static_cast<long>(header_.image_offset), SEEK_SET);
    for (uint16_t i = 0; i < header_.image_count; ++i) {
      uint8_t buf[8];
      if (!read_bytes(buf, 8)) {
        close();
        return false;
      }
      images_[i].zip_entry_index = mrb_read_u16(buf);
      images_[i].width = mrb_read_u16(buf + 2);
      images_[i].height = mrb_read_u16(buf + 4);
      images_[i].reserved = mrb_read_u16(buf + 6);
    }
  }

  // Read metadata
  fseek(f_, static_cast<long>(header_.meta_offset), SEEK_SET);
  metadata_.title = read_string();
  std::string author = read_string();
  if (!author.empty())
    metadata_.author = std::move(author);
  std::string lang = read_string();
  if (!lang.empty())
    metadata_.language = std::move(lang);

  // Read TOC
  uint8_t toc_hdr[2];
  if (read_bytes(toc_hdr, 2)) {
    uint16_t toc_count = mrb_read_u16(toc_hdr);
    toc_.entries.resize(toc_count);
    for (uint16_t i = 0; i < toc_count; ++i) {
      toc_.entries[i].label = read_string();
      uint8_t fidx[2];
      if (read_bytes(fidx, 2))
        toc_.entries[i].file_idx = mrb_read_u16(fidx);
    }
  }

  return true;
}

void MrbReader::close() {
  if (f_) {
    fclose(f_);
    f_ = nullptr;
  }
  chapters_.clear();
  chapters_.shrink_to_fit();
  images_.clear();
  images_.shrink_to_fit();
  header_ = {};
  metadata_ = {};
  toc_ = {};
}

uint32_t MrbReader::chapter_first_paragraph(uint16_t chapter_idx) const {
  if (chapter_idx >= chapters_.size())
    return 0;
  return chapters_[chapter_idx].first_paragraph;
}

uint16_t MrbReader::chapter_paragraph_count(uint16_t chapter_idx) const {
  if (chapter_idx >= chapters_.size())
    return 0;
  return chapters_[chapter_idx].paragraph_count;
}

bool MrbReader::load_paragraph(uint32_t index, Paragraph& out) {
  if (!f_ || index >= header_.paragraph_count)
    return false;

  // Seek to the paragraph's index entry (8 bytes each) to get file offset.
  uint8_t idx_buf[8];
  if (!read_at(header_.index_offset + index * 8, idx_buf, 8))
    return false;
  uint32_t para_offset = mrb_read_u32(idx_buf);

  fseek(f_, static_cast<long>(para_offset), SEEK_SET);

  // Read type + data_size (uint32)
  uint8_t hdr[5];
  if (!read_bytes(hdr, 5))
    return false;

  uint8_t type = hdr[0];
  uint32_t data_size = mrb_read_u32(hdr + 1);

  // Read body
  std::vector<uint8_t> body(data_size);
  if (data_size > 0 && !read_bytes(body.data(), data_size))
    return false;

  switch (type) {
    case kMrbParaText:
      return deserialize_text(body.data(), body.size(), out);

    case kMrbParaImage: {
      out = Paragraph{};
      out.type = ParagraphType::Image;
      if (data_size >= 4) {
        uint16_t key = mrb_read_u16(body.data());
        // Look up dimensions from image ref table
        uint16_t w = 0, h = 0;
        if (key < images_.size()) {
          w = images_[key].width;
          h = images_[key].height;
        }
        out.image = ImageRef{key, w, h};
        uint16_t sp = mrb_read_u16(body.data() + 2);
        if (sp != kMrbSpacingDefault)
          out.spacing_before = sp;
      }
      return true;
    }

    case kMrbParaHr: {
      out = Paragraph::make_hr();
      if (data_size >= 2) {
        uint16_t sp = mrb_read_u16(body.data());
        if (sp != kMrbSpacingDefault)
          out.spacing_before = sp;
      }
      return true;
    }

    case kMrbParaPageBreak:
      out = Paragraph::make_page_break();
      return true;

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Text paragraph deserialization
// ---------------------------------------------------------------------------

bool MrbReader::deserialize_text(const uint8_t* data, size_t size, Paragraph& out) {
  out = Paragraph{};
  out.type = ParagraphType::Text;

  if (size < 18)  // minimum: header fields + run_count
    return false;

  size_t pos = 0;

  // alignment
  uint8_t align_val = data[pos++];
  if (align_val != kMrbAlignDefault)
    out.text.alignment = static_cast<Alignment>(align_val);

  // indent
  int16_t indent_val = mrb_read_i16(data + pos);
  pos += 2;
  if (indent_val != kMrbIndentNone)
    out.text.indent = indent_val;

  // margin_left, margin_right (paragraph-level, currently unused placeholders)
  pos += 2;  // skip margin_left
  pos += 2;  // skip margin_right

  // spacing_before
  uint16_t spacing = mrb_read_u16(data + pos);
  pos += 2;
  if (spacing != kMrbSpacingDefault)
    out.spacing_before = spacing;

  // line_height_pct
  out.text.line_height_pct = data[pos++];

  // inline image
  uint16_t img_key = mrb_read_u16(data + pos);
  pos += 2;
  uint16_t img_w = mrb_read_u16(data + pos);
  pos += 2;
  uint16_t img_h = mrb_read_u16(data + pos);
  pos += 2;
  if (img_key != kMrbNoImage)
    out.text.inline_image = ImageRef{img_key, img_w, img_h};

  // run count
  if (pos + 2 > size)
    return false;
  uint16_t run_count = mrb_read_u16(data + pos);
  pos += 2;

  out.text.runs.resize(run_count);
  for (uint16_t i = 0; i < run_count; ++i) {
    if (pos + 12 > size)
      return false;

    Run& run = out.text.runs[i];
    run.style = static_cast<FontStyle>(data[pos++]);
    run.size = static_cast<FontSize>(data[pos++]);
    run.vertical_align = static_cast<VerticalAlign>(data[pos++]);
    uint8_t flags = data[pos++];
    run.breaking = (flags & 0x01) != 0;

    run.margin_left = mrb_read_u16(data + pos);
    pos += 2;
    run.margin_right = mrb_read_u16(data + pos);
    pos += 2;

    uint32_t text_len = mrb_read_u32(data + pos);
    pos += 4;

    if (pos + text_len > size)
      return false;
    run.text.assign(reinterpret_cast<const char*>(data + pos), text_len);
    pos += text_len;
  }

  return true;
}

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------

bool MrbReader::read_bytes(void* buf, size_t size) {
  return fread(buf, 1, size, f_) == size;
}

bool MrbReader::read_at(uint32_t offset, void* buf, size_t size) {
  fseek(f_, static_cast<long>(offset), SEEK_SET);
  return read_bytes(buf, size);
}

std::string MrbReader::read_string() {
  uint8_t len_buf[2];
  if (!read_bytes(len_buf, 2))
    return {};
  uint16_t len = mrb_read_u16(len_buf);
  if (len == 0)
    return {};
  std::string s(len, '\0');
  if (!read_bytes(s.data(), len))
    return {};
  return s;
}

}  // namespace microreader
