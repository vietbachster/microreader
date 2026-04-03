#pragma once

#include <string>
#include <vector>

#include "ContentModel.h"
#include "CssParser.h"
#include "ZipReader.h"

namespace microreader {

enum class EpubError {
  Ok = 0,
  ContainerMissing,
  ContentOpfMissing,
  InvalidData,
  ZipError,
  XmlError,
};

// EPUB book: parsed from an EPUB file.
// Stores the ZIP entries, spine, metadata, and global stylesheet.
// Chapters are parsed on-demand via parse_chapter().
class Epub {
 public:
  Epub() = default;

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    stylesheet_.set_config(config);
  }
  const CssConfig& css_config() const {
    return stylesheet_.config();
  }

  // Parse an EPUB file. Opens the ZIP, reads container.xml, content.opf,
  // TOC, and stylesheets.
  EpubError open(IZipFile& file);

  // Number of chapters (spine items).
  size_t chapter_count() const {
    return spine_.size();
  }

  // Parse a specific chapter by index.
  EpubError parse_chapter(IZipFile& file, size_t index, Chapter& out) const;

  // Access metadata.
  const EpubMetadata& metadata() const {
    return metadata_;
  }

  // Access TOC.
  const TableOfContents& toc() const {
    return toc_;
  }

  // Access the zip reader (for image extraction etc)
  const ZipReader& zip() const {
    return zip_;
  }
  const std::vector<SpineItem>& spine() const {
    return spine_;
  }
  const CssStylesheet& stylesheet() const {
    return stylesheet_;
  }

  // Resolve a path relative to a content file's directory.
  // e.g. resolve_path("OEBPS/chapters/", "../images/test.jpg") → "OEBPS/images/test.jpg"
  static std::string resolve_path(const std::string& base_dir, const std::string& href);

  // Find an entry index by path.
  const ZipEntry* find_entry(const std::string& path) const {
    return zip_.find(path);
  }
  int find_entry_index(const std::string& path) const;

 private:
  ZipReader zip_;
  std::string root_dir_;  // e.g. "OEBPS/"
  EpubMetadata metadata_;
  std::vector<SpineItem> spine_;
  TableOfContents toc_;
  CssStylesheet stylesheet_;
  int cover_idx_ = -1;

  // Internal parsing steps
  EpubError parse_container(IZipFile& file, std::string& rootfile_path);
  EpubError parse_opf(IZipFile& file, const std::string& opf_path);
};

// Parse XHTML body into paragraphs (used by Epub::parse_chapter, also
// usable standalone for testing).
EpubError parse_xhtml_body(const uint8_t* data, size_t size, const CssStylesheet* inline_css,
                           const CssStylesheet* extern_css, const std::string& base_dir, const ZipReader& zip,
                           std::vector<Paragraph>& out);

}  // namespace microreader
