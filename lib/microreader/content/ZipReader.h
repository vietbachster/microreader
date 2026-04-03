#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace microreader {

// Error codes for ZIP operations.
enum class ZipError {
  Ok = 0,
  FileNotFound,
  InvalidSignature,
  InvalidData,
  UnsupportedCompression,
  DecompressionFailed,
  ReadError,
};

// Metadata for a single file inside a ZIP archive.
struct ZipEntry {
  std::string name;
  uint32_t uncompressed_size = 0;
  uint32_t compressed_size = 0;
  uint32_t local_header_offset = 0;  // offset to the local file header
  uint16_t compression = 0;          // 0=stored, 8=deflate
};

// Callback that receives decompressed data chunks.
// Return true to continue, false to abort.
using ZipDataCallback = bool (*)(const uint8_t* data, size_t size, void* user_data);

// Abstract file interface for ZIP reading.
// This allows the ZIP reader to work with FILE*, SD cards, or memory buffers.
class IZipFile {
 public:
  virtual ~IZipFile() = default;
  virtual bool seek(int64_t offset, int whence) = 0;  // SEEK_SET, SEEK_CUR, SEEK_END
  virtual int64_t tell() = 0;
  virtual size_t read(void* buf, size_t size) = 0;
};

// Reads a ZIP archive's central directory and provides streaming extraction.
//
// Usage:
//   StdioZipFile file("book.epub");
//   ZipReader reader;
//   if (reader.open(file) == ZipError::Ok) {
//     auto* entry = reader.find("META-INF/container.xml");
//     std::vector<uint8_t> buf;
//     reader.extract(file, *entry, buf);
//   }
class ZipReader {
 public:
  ZipReader() = default;

  // Parse the central directory. Populates entries().
  ZipError open(IZipFile& file);

  // Number of entries in the archive.
  size_t entry_count() const { return entries_.size(); }

  // Access entries by index.
  const ZipEntry& entry(size_t index) const { return entries_[index]; }
  const std::vector<ZipEntry>& entries() const { return entries_; }

  // Find an entry by name. Returns nullptr if not found.
  const ZipEntry* find(const char* name) const;
  const ZipEntry* find(const std::string& name) const { return find(name.c_str()); }

  // Extract an entry into a pre-allocated vector (resized to uncompressed_size).
  ZipError extract(IZipFile& file, const ZipEntry& entry, std::vector<uint8_t>& out) const;

  // Extract an entry via streaming callback. Uses a small fixed buffer.
  // work_buf must be at least 33KB for deflate (32KB dict + 1KB input).
  ZipError extract_streaming(IZipFile& file, const ZipEntry& entry,
                             ZipDataCallback callback, void* user_data,
                             uint8_t* work_buf, size_t work_buf_size) const;

 private:
  std::vector<ZipEntry> entries_;
};

// IZipFile implementation backed by FILE* (for desktop and tests).
class StdioZipFile : public IZipFile {
 public:
  StdioZipFile() = default;
  ~StdioZipFile() override;

  bool open(const char* path);
  void close();
  bool is_open() const;

  bool seek(int64_t offset, int whence) override;
  int64_t tell() override;
  size_t read(void* buf, size_t size) override;

 private:
  void* fp_ = nullptr;  // FILE*, but we avoid including <cstdio> in the header
};

}  // namespace microreader
