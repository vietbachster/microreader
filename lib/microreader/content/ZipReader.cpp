#include "ZipReader.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

// We only need the tinfl decompressor from miniz.
// Define MINIZ_NO_ZLIB_COMPATIBLE_NAMES to avoid polluting the namespace.
#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS
#include "miniz.h"

namespace microreader {

// ---------------------------------------------------------------------------
// ZIP format constants and on-disk structures
// ---------------------------------------------------------------------------

static constexpr uint32_t kEndCentralDirSig = 0x06054b50;
static constexpr uint32_t kCentralDirEntrySig = 0x02014b50;
static constexpr uint32_t kLocalHeaderSig = 0x04034b50;

#pragma pack(push, 1)
struct EndCentralDir {
  uint32_t signature;
  uint16_t disk_number;
  uint16_t central_dir_start_disk;
  uint16_t num_entries_this_disk;
  uint16_t total_entries;
  uint32_t central_dir_size;
  uint32_t central_dir_offset;
  uint16_t comment_length;
};

struct CentralDirEntry {
  uint32_t signature;
  uint16_t version_made;
  uint16_t version_needed;
  uint16_t flags;
  uint16_t compression;
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_len;
  uint16_t comment_len;
  uint16_t disk_start;
  uint16_t internal_attr;
  uint32_t external_attr;
  uint32_t local_header_offset;
};

struct LocalFileHeader {
  uint32_t signature;
  uint16_t version_needed;
  uint16_t flags;
  uint16_t compression;
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t compressed_size;
  uint32_t uncompressed_size;
  uint16_t filename_len;
  uint16_t extra_len;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool read_exact(IZipFile& file, void* buf, size_t size) {
  size_t got = file.read(buf, size);
  return got == size;
}

static ZipError find_end_central_dir(IZipFile& file, EndCentralDir& eocd) {
  uint8_t buf[1024];

  // Seek to last 1024 bytes (or start of file)
  file.seek(0, SEEK_END);
  int64_t file_size = file.tell();
  if (file_size < static_cast<int64_t>(sizeof(EndCentralDir))) {
    return ZipError::InvalidData;
  }

  int64_t search_start = (file_size > 1024) ? (file_size - 1024) : 0;
  size_t search_len = static_cast<size_t>(file_size - search_start);
  file.seek(search_start, SEEK_SET);
  if (file.read(buf, search_len) != search_len) {
    return ZipError::ReadError;
  }

  // Search backwards for the end-of-central-directory signature
  for (int i = static_cast<int>(search_len) - static_cast<int>(sizeof(EndCentralDir)); i >= 0; --i) {
    uint32_t sig;
    memcpy(&sig, &buf[i], 4);
    if (sig == kEndCentralDirSig) {
      memcpy(&eocd, &buf[i], sizeof(EndCentralDir));
      return ZipError::Ok;
    }
  }
  return ZipError::InvalidSignature;
}

// ---------------------------------------------------------------------------
// ZipReader
// ---------------------------------------------------------------------------

ZipError ZipReader::open(IZipFile& file) {
  entries_.clear();

  EndCentralDir eocd{};
  ZipError err = find_end_central_dir(file, eocd);
  if (err != ZipError::Ok) return err;

  if (eocd.total_entries == 0) return ZipError::InvalidData;

  entries_.reserve(eocd.total_entries);

  file.seek(eocd.central_dir_offset, SEEK_SET);

  for (uint16_t i = 0; i < eocd.total_entries; ++i) {
    CentralDirEntry cde{};
    if (!read_exact(file, &cde, sizeof(cde))) return ZipError::ReadError;
    if (cde.signature != kCentralDirEntrySig) return ZipError::InvalidSignature;

    // Read filename
    std::string name(cde.filename_len, '\0');
    if (!read_exact(file, name.data(), cde.filename_len)) return ZipError::ReadError;

    // Skip extra field and comment
    if (cde.extra_len > 0) file.seek(cde.extra_len, SEEK_CUR);
    if (cde.comment_len > 0) file.seek(cde.comment_len, SEEK_CUR);

    ZipEntry entry;
    entry.name = std::move(name);
    entry.uncompressed_size = cde.uncompressed_size;
    entry.compressed_size = cde.compressed_size;
    entry.local_header_offset = cde.local_header_offset;
    entry.compression = cde.compression;
    entries_.push_back(std::move(entry));
  }

  return ZipError::Ok;
}

const ZipEntry* ZipReader::find(const char* name) const {
  for (const auto& e : entries_) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

// Seek to the start of the compressed data for an entry.
static ZipError seek_to_data(IZipFile& file, const ZipEntry& entry) {
  file.seek(entry.local_header_offset, SEEK_SET);

  LocalFileHeader lfh{};
  if (!read_exact(file, &lfh, sizeof(lfh))) return ZipError::ReadError;
  if (lfh.signature != kLocalHeaderSig) return ZipError::InvalidSignature;

  // Skip filename + extra field to reach the data
  int64_t skip = static_cast<int64_t>(lfh.filename_len) + lfh.extra_len;
  file.seek(skip, SEEK_CUR);

  return ZipError::Ok;
}

ZipError ZipReader::extract(IZipFile& file, const ZipEntry& entry, std::vector<uint8_t>& out) const {
  out.resize(entry.uncompressed_size);
  if (entry.uncompressed_size == 0) return ZipError::Ok;

  size_t offset = 0;
  auto cb = [](const uint8_t* data, size_t size, void* user_data) -> bool {
    auto* ctx = static_cast<std::pair<std::vector<uint8_t>*, size_t>*>(user_data);
    if (ctx->second + size > ctx->first->size()) return false;
    memcpy(ctx->first->data() + ctx->second, data, size);
    ctx->second += size;
    return true;
  };

  std::pair<std::vector<uint8_t>*, size_t> ctx{&out, 0};

  // Use a stack-allocated work buffer for extract()
  // 32KB dict + 1KB input = 33KB
  static constexpr size_t kWorkBufSize = TINFL_LZ_DICT_SIZE + 1024;
  std::vector<uint8_t> work_buf(kWorkBufSize);

  return extract_streaming(file, entry, cb, &ctx, work_buf.data(), work_buf.size());
}

ZipError ZipReader::extract_streaming(IZipFile& file, const ZipEntry& entry,
                                      ZipDataCallback callback, void* user_data,
                                      uint8_t* work_buf, size_t work_buf_size) const {
  ZipError err = seek_to_data(file, entry);
  if (err != ZipError::Ok) return err;

  if (entry.compression == 0) {
    // Stored: read directly, use work_buf as copy buffer
    size_t chunk = std::min(work_buf_size, static_cast<size_t>(4096));
    size_t remaining = entry.uncompressed_size;
    while (remaining > 0) {
      size_t to_read = std::min(remaining, chunk);
      size_t got = file.read(work_buf, to_read);
      if (got == 0) return ZipError::ReadError;
      if (!callback(work_buf, got, user_data)) return ZipError::Ok;  // user abort
      remaining -= got;
    }
    return ZipError::Ok;
  }

  if (entry.compression != 8) {
    return ZipError::UnsupportedCompression;
  }

  // Deflate: use tinfl with dictionary/wrapping mode (raw deflate, no zlib header).
  // Layout of work_buf: [dict(32KB) | input_buf(remaining)]
  // The decompressor is heap-allocated to guarantee alignment.
  size_t min_size = TINFL_LZ_DICT_SIZE + 256;
  if (work_buf_size < min_size) return ZipError::DecompressionFailed;

  tinfl_decompressor decomp_storage;
  tinfl_decompressor* decomp = &decomp_storage;
  uint8_t* dict = work_buf;
  uint8_t* in_buf = work_buf + TINFL_LZ_DICT_SIZE;
  size_t in_buf_capacity = work_buf_size - TINFL_LZ_DICT_SIZE;

  tinfl_init(decomp);

  size_t in_remaining = entry.compressed_size;
  size_t in_buf_size = 0;
  size_t in_buf_ofs = 0;
  size_t dict_ofs = 0;

  for (;;) {
    // Refill input buffer if needed
    if (in_buf_ofs >= in_buf_size && in_remaining > 0) {
      size_t to_read = std::min(in_remaining, in_buf_capacity);
      in_buf_size = file.read(in_buf, to_read);
      if (in_buf_size == 0) return ZipError::ReadError;
      in_remaining -= in_buf_size;
      in_buf_ofs = 0;
    }

    size_t in_bytes = in_buf_size - in_buf_ofs;
    size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;

    mz_uint32 flags = 0;
    if (in_remaining > 0) flags |= TINFL_FLAG_HAS_MORE_INPUT;

    tinfl_status status = tinfl_decompress(
        decomp, in_buf + in_buf_ofs, &in_bytes,
        dict, dict + dict_ofs, &out_bytes, flags);

    in_buf_ofs += in_bytes;

    if (out_bytes > 0) {
      if (!callback(dict + dict_ofs, out_bytes, user_data)) {
        return ZipError::Ok;  // user abort
      }
      dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
    }

    if (status == TINFL_STATUS_DONE) break;
    if (status < TINFL_STATUS_DONE) return ZipError::DecompressionFailed;
  }

  return ZipError::Ok;
}

// ---------------------------------------------------------------------------
// StdioZipFile
// ---------------------------------------------------------------------------

StdioZipFile::~StdioZipFile() {
  close();
}

bool StdioZipFile::open(const char* path) {
  close();
  fp_ = std::fopen(path, "rb");
  return fp_ != nullptr;
}

void StdioZipFile::close() {
  if (fp_) {
    std::fclose(static_cast<FILE*>(fp_));
    fp_ = nullptr;
  }
}

bool StdioZipFile::is_open() const {
  return fp_ != nullptr;
}

bool StdioZipFile::seek(int64_t offset, int whence) {
  if (!fp_) return false;
#ifdef _WIN32
  return _fseeki64(static_cast<FILE*>(fp_), offset, whence) == 0;
#else
  return fseek(static_cast<FILE*>(fp_), static_cast<long>(offset), whence) == 0;
#endif
}

int64_t StdioZipFile::tell() {
  if (!fp_) return -1;
#ifdef _WIN32
  return _ftelli64(static_cast<FILE*>(fp_));
#else
  return ftell(static_cast<FILE*>(fp_));
#endif
}

size_t StdioZipFile::read(void* buf, size_t size) {
  if (!fp_) return 0;
  return std::fread(buf, 1, size, static_cast<FILE*>(fp_));
}

}  // namespace microreader
