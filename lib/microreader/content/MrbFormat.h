#pragma once

// MRB (MicroReader Book) — pre-processed binary format for fast on-device
// EPUB reading.  Stores paragraphs with styling and text inline, plus index
// tables for O(1) random access.
//
// File layout (all values little-endian):
//
//   [Header 32 bytes]
//   [Paragraph data — variable, written sequentially]
//   [Paragraph index — paragraph_count × 8 bytes]
//   [Chapter table  — chapter_count  × 8 bytes]
//   [Image ref table— image_count    × 8 bytes]
//   [Metadata blob  — variable]
//   [TOC blob       — variable]

#include <cstdint>
#include <cstring>

namespace microreader {

// ---------------------------------------------------------------------------
// Magic & version
// ---------------------------------------------------------------------------

static constexpr uint8_t kMrbMagic[4] = {'M', 'R', 'B', '1'};
static constexpr uint16_t kMrbVersion = 1;

// ---------------------------------------------------------------------------
// Header (32 bytes, fixed)
// ---------------------------------------------------------------------------

struct MrbHeader {
  uint8_t magic[4];  // "MRB1"
  uint16_t version;  // 1
  uint16_t flags;    // reserved (0)
  uint32_t paragraph_count;
  uint16_t chapter_count;
  uint16_t image_count;
  uint32_t index_offset;    // file offset of paragraph index
  uint32_t chapter_offset;  // file offset of chapter table
  uint32_t image_offset;    // file offset of image ref table
  uint32_t meta_offset;     // file offset of metadata blob
};
static_assert(sizeof(MrbHeader) == 32, "MrbHeader must be 32 bytes");

// ---------------------------------------------------------------------------
// Paragraph index entry (8 bytes each)
// ---------------------------------------------------------------------------

struct MrbParagraphIndex {
  uint32_t file_offset;    // offset into file where paragraph data begins
  uint16_t chapter_index;  // which chapter this paragraph belongs to
  uint8_t type;            // ParagraphType (0=Text, 1=Image, 2=Hr, 3=PageBreak)
  uint8_t reserved;
};
static_assert(sizeof(MrbParagraphIndex) == 8, "MrbParagraphIndex must be 8 bytes");

// ---------------------------------------------------------------------------
// Chapter table entry (8 bytes each)
// ---------------------------------------------------------------------------

struct MrbChapterEntry {
  uint32_t first_paragraph;  // index into paragraph index
  uint16_t paragraph_count;
  uint16_t reserved;
};
static_assert(sizeof(MrbChapterEntry) == 8, "MrbChapterEntry must be 8 bytes");

// ---------------------------------------------------------------------------
// Image reference entry (8 bytes each)
// ---------------------------------------------------------------------------

struct MrbImageRef {
  uint16_t zip_entry_index;  // index into the EPUB's ZIP entries
  uint16_t width;
  uint16_t height;
  uint16_t reserved;
};
static_assert(sizeof(MrbImageRef) == 8, "MrbImageRef must be 8 bytes");

// ---------------------------------------------------------------------------
// Paragraph type tags (match ParagraphType enum values)
// ---------------------------------------------------------------------------

static constexpr uint8_t kMrbParaText = 0;
static constexpr uint8_t kMrbParaImage = 1;
static constexpr uint8_t kMrbParaHr = 2;
static constexpr uint8_t kMrbParaPageBreak = 3;

// ---------------------------------------------------------------------------
// Sentinel values for optional fields
// ---------------------------------------------------------------------------

static constexpr uint8_t kMrbAlignDefault = 0xFF;
static constexpr int16_t kMrbIndentNone = 0x7FFF;
static constexpr uint16_t kMrbSpacingDefault = 0xFFFF;
static constexpr uint16_t kMrbNoImage = 0xFFFF;

// ---------------------------------------------------------------------------
// Little-endian serialization helpers
// ---------------------------------------------------------------------------

inline void mrb_write_u8(uint8_t* dst, uint8_t v) {
  dst[0] = v;
}
inline void mrb_write_u16(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
}
inline void mrb_write_i16(uint8_t* dst, int16_t v) {
  mrb_write_u16(dst, static_cast<uint16_t>(v));
}
inline void mrb_write_u32(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v);
  dst[1] = static_cast<uint8_t>(v >> 8);
  dst[2] = static_cast<uint8_t>(v >> 16);
  dst[3] = static_cast<uint8_t>(v >> 24);
}

inline uint8_t mrb_read_u8(const uint8_t* src) {
  return src[0];
}
inline uint16_t mrb_read_u16(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}
inline int16_t mrb_read_i16(const uint8_t* src) {
  return static_cast<int16_t>(mrb_read_u16(src));
}
inline uint32_t mrb_read_u32(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8) | (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

}  // namespace microreader
