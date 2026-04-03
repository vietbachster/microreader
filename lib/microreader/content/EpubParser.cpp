#include "EpubParser.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "ImageDecoder.h"
#include "XmlReader.h"

namespace microreader {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string sv_to_string(XmlStringView sv) {
  if (!sv.data || sv.length == 0)
    return {};
  return std::string(sv.data, sv.length);
}

static bool sv_eq(XmlStringView sv, const char* s) {
  return sv == s;
}

// Extract just the file data for a given zip entry into a vector.
static EpubError extract_entry(IZipFile& file, const ZipReader& zip, const ZipEntry& entry, std::vector<uint8_t>& out) {
  if (zip.extract(file, entry, out) != ZipError::Ok) {
    return EpubError::ZipError;
  }
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

std::string Epub::resolve_path(const std::string& base_dir, const std::string& href) {
  std::string path = href;

  // Strip leading "./"
  while (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
    path = path.substr(2);
  }

  std::string dir = base_dir;

  // Handle "../"
  while (path.size() >= 3 && path[0] == '.' && path[1] == '.' && path[2] == '/') {
    path = path.substr(3);
    // Go up one directory
    if (!dir.empty() && dir.back() == '/')
      dir.pop_back();
    auto pos = dir.rfind('/');
    if (pos != std::string::npos) {
      dir = dir.substr(0, pos + 1);
    } else {
      dir.clear();
    }
  }

  return dir + path;
}

int Epub::find_entry_index(const std::string& path) const {
  for (size_t i = 0; i < zip_.entry_count(); ++i) {
    if (zip_.entry(i).name == path)
      return static_cast<int>(i);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Container.xml parsing
// ---------------------------------------------------------------------------

EpubError Epub::parse_container(IZipFile& file, std::string& rootfile_path) {
  auto* entry = zip_.find("META-INF/container.xml");
  if (!entry)
    return EpubError::ContainerMissing;

  std::vector<uint8_t> data;
  auto err = extract_entry(file, zip_, *entry, data);
  if (err != EpubError::Ok)
    return err;

  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[512];
  XmlReader reader;
  if (reader.open(input, buf, sizeof(buf)) != XmlError::Ok)
    return EpubError::XmlError;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement && sv_eq(ev.name, "rootfile")) {
      auto fp = ev.attrs.get("full-path");
      if (!fp.empty()) {
        rootfile_path = sv_to_string(fp);
        return EpubError::Ok;
      }
    }
  }
  return EpubError::ContentOpfMissing;
}

// ---------------------------------------------------------------------------
// OPF parsing (metadata, manifest, spine)
// ---------------------------------------------------------------------------

namespace {

enum class MediaType : uint8_t {
  Image,
  Xhtml,
  Css,
  Ncx,
  Unknown,
};

MediaType parse_media_type(const std::string& s) {
  if (s == "image/jpeg" || s == "image/png" || s == "image/gif")
    return MediaType::Image;
  if (s == "application/xhtml+xml")
    return MediaType::Xhtml;
  if (s == "text/css")
    return MediaType::Css;
  if (s == "application/x-dtbncx+xml")
    return MediaType::Ncx;
  return MediaType::Unknown;
}

struct ManifestItem {
  MediaType media_type;
  std::string href;
  int file_idx;
};

}  // namespace

// NOTE: OPF parsing uses a flat single-pass design instead of hierarchical
// sub-parsers. This avoids XmlReader buffer state issues where nested parse
// functions break on large OPFs (e.g. 77K+ bytes with 500+ manifest items).

// Parse NCX table of contents
static EpubError parse_ncx(IZipFile& file, const ZipReader& zip, const ZipEntry& entry, const std::string& root_dir,
                           TableOfContents& toc) {
  std::vector<uint8_t> data;
  if (zip.extract(file, entry, data) != ZipError::Ok)
    return EpubError::ZipError;

  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[4096];
  XmlReader reader;
  if (reader.open(input, buf, sizeof(buf)) != XmlError::Ok)
    return EpubError::XmlError;

  // Simple NCX parsing: find navPoint → navLabel → text, content[@src]
  std::string current_label;
  bool in_nav_point = false;
  bool in_nav_label = false;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;

    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "navPoint")) {
        in_nav_point = true;
        current_label.clear();
      } else if (in_nav_point && sv_eq(ev.name, "navLabel")) {
        in_nav_label = true;
      } else if (in_nav_label && sv_eq(ev.name, "text")) {
        XmlEvent text;
        if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
          current_label = sv_to_string(text.content);
        }
      } else if (in_nav_point && sv_eq(ev.name, "content")) {
        auto src = sv_to_string(ev.attrs.get("src"));
        if (!src.empty()) {
          // Strip fragment
          auto hash = src.find('#');
          if (hash != std::string::npos)
            src = src.substr(0, hash);

          std::string full_path = root_dir + src;
          int idx = -1;
          for (size_t i = 0; i < zip.entry_count(); ++i) {
            if (zip.entry(i).name == full_path) {
              idx = static_cast<int>(i);
              break;
            }
          }
          if (idx >= 0) {
            toc.entries.push_back({current_label, static_cast<uint16_t>(idx)});
          }
        }
      }
    } else if (ev.type == XmlEventType::EndElement) {
      if (sv_eq(ev.name, "navPoint"))
        in_nav_point = false;
      if (sv_eq(ev.name, "navLabel"))
        in_nav_label = false;
    }
  }
  return EpubError::Ok;
}

EpubError Epub::parse_opf(IZipFile& file, const std::string& opf_path) {
  auto* entry = zip_.find(opf_path);
  if (!entry)
    return EpubError::ContentOpfMissing;

  std::vector<uint8_t> data;
  auto err = extract_entry(file, zip_, *entry, data);
  if (err != EpubError::Ok)
    return err;

  MemoryXmlInput input(data.data(), data.size());
  // Use a buffer large enough to hold the entire OPF at once — avoids
  // XmlReader streaming boundary issues on large OPFs (e.g. 77K+ bytes).
  std::vector<uint8_t> buf(std::max(data.size() + 1, size_t(4096)));
  XmlReader reader;
  if (reader.open(input, buf.data(), buf.size()) != XmlError::Ok)
    return EpubError::XmlError;

  std::map<std::string, ManifestItem> manifest;
  int ncx_file_idx = -1;
  std::string toc_id_ref;

  // Track which section we're in for context
  enum class Section { None, Metadata, Manifest, Spine } section = Section::None;

  // Flat single-pass: process all events without delegating to sub-parsers.
  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;

    if (ev.type == XmlEventType::StartElement) {
      // Section transitions
      if (sv_eq(ev.name, "metadata") || sv_eq(ev.name, "opf:metadata")) {
        section = Section::Metadata;
        continue;
      } else if (sv_eq(ev.name, "manifest") || sv_eq(ev.name, "opf:manifest")) {
        section = Section::Manifest;
        continue;
      } else if (sv_eq(ev.name, "spine") || sv_eq(ev.name, "opf:spine")) {
        section = Section::Spine;
        toc_id_ref = sv_to_string(ev.attrs.get("toc"));
        continue;
      }

      // Handle elements per section
      if (section == Section::Metadata) {
        if (sv_eq(ev.name, "dc:title") || sv_eq(ev.name, "title")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (metadata_.title.empty())
              metadata_.title = sv_to_string(text.content);
          }
        } else if (sv_eq(ev.name, "dc:creator") || sv_eq(ev.name, "creator")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (!metadata_.author.has_value())
              metadata_.author = sv_to_string(text.content);
          }
        } else if (sv_eq(ev.name, "dc:language") || sv_eq(ev.name, "language")) {
          XmlEvent text;
          if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
            if (!metadata_.language.has_value())
              metadata_.language = sv_to_string(text.content);
          }
        } else if (sv_eq(ev.name, "meta")) {
          auto name = ev.attrs.get("name");
          if (sv_eq(name, "cover")) {
            auto content = ev.attrs.get("content");
            if (!content.empty()) {
              metadata_.cover_id = sv_to_string(content);
            }
          }
        }
      } else if (section == Section::Manifest) {
        if (sv_eq(ev.name, "item")) {
          auto id = sv_to_string(ev.attrs.get("id"));
          auto href = sv_to_string(ev.attrs.get("href"));
          auto mt = sv_to_string(ev.attrs.get("media-type"));

          if (!id.empty() && !href.empty()) {
            std::string full_path = root_dir_ + href;
            int idx = -1;
            for (size_t i = 0; i < zip_.entry_count(); ++i) {
              if (zip_.entry(i).name == full_path) {
                idx = static_cast<int>(i);
                break;
              }
            }
            manifest[id] = {parse_media_type(mt), href, idx};
          }
        }
      } else if (section == Section::Spine) {
        if (sv_eq(ev.name, "itemref")) {
          auto idref = sv_to_string(ev.attrs.get("idref"));
          auto it = manifest.find(idref);
          if (it != manifest.end() && it->second.file_idx >= 0) {
            spine_.push_back({static_cast<uint16_t>(it->second.file_idx)});
          }
        }
      }
    } else if (ev.type == XmlEventType::EndElement) {
      if (sv_eq(ev.name, "metadata") || sv_eq(ev.name, "opf:metadata") || sv_eq(ev.name, "manifest") ||
          sv_eq(ev.name, "opf:manifest") || sv_eq(ev.name, "spine") || sv_eq(ev.name, "opf:spine")) {
        section = Section::None;
      }
    }
  }

  // Resolve toc NCX reference
  if (!toc_id_ref.empty()) {
    auto it = manifest.find(toc_id_ref);
    if (it != manifest.end() && it->second.media_type == MediaType::Ncx) {
      ncx_file_idx = it->second.file_idx;
    }
  }

  // Find cover in manifest
  if (metadata_.cover_id.has_value()) {
    auto it = manifest.find(*metadata_.cover_id);
    if (it != manifest.end()) {
      cover_idx_ = it->second.file_idx;
    }
  }

  // Parse stylesheets
  for (auto& [id, item] : manifest) {
    if (item.media_type == MediaType::Css && item.file_idx >= 0) {
      auto& css_entry = zip_.entry(item.file_idx);
      std::vector<uint8_t> css_data;
      if (zip_.extract(file, css_entry, css_data) == ZipError::Ok) {
        stylesheet_.extend_from_sheet(reinterpret_cast<const char*>(css_data.data()), css_data.size());
      }
    }
  }

  // Parse NCX
  if (ncx_file_idx >= 0) {
    auto& ncx_entry = zip_.entry(ncx_file_idx);
    parse_ncx(file, zip_, ncx_entry, root_dir_, toc_);
  }

  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Epub::open
// ---------------------------------------------------------------------------

EpubError Epub::open(IZipFile& file) {
  if (zip_.open(file) != ZipError::Ok)
    return EpubError::ZipError;

  std::string rootfile_path;
  auto err = parse_container(file, rootfile_path);
  if (err != EpubError::Ok)
    return err;

  // Determine root directory
  auto slash = rootfile_path.rfind('/');
  if (slash != std::string::npos) {
    root_dir_ = rootfile_path.substr(0, slash + 1);
  } else {
    root_dir_.clear();
  }

  return parse_opf(file, rootfile_path);
}

// ---------------------------------------------------------------------------
// XHTML body → Paragraphs
// ---------------------------------------------------------------------------

namespace {

// Decode basic HTML entities
static std::string decode_entities(const std::string& text) {
  std::string result;
  result.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '&') {
      size_t semi = text.find(';', i);
      if (semi != std::string::npos && semi - i < 12) {
        std::string entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp")
          result += '&';
        else if (entity == "lt")
          result += '<';
        else if (entity == "gt")
          result += '>';
        else if (entity == "quot")
          result += '"';
        else if (entity == "apos")
          result += '\'';
        else if (entity == "nbsp")
          result += ' ';
        else if (!entity.empty() && entity[0] == '#') {
          // Numeric entity
          uint32_t code = 0;
          if (entity.size() > 1 && entity[1] == 'x') {
            for (size_t j = 2; j < entity.size(); ++j)
              code = code * 16 + (std::isdigit(static_cast<unsigned char>(entity[j]))
                                      ? entity[j] - '0'
                                      : std::tolower(static_cast<unsigned char>(entity[j])) - 'a' + 10);
          } else {
            for (size_t j = 1; j < entity.size(); ++j)
              code = code * 10 + (entity[j] - '0');
          }
          // UTF-8 encode
          if (code < 0x80) {
            result += static_cast<char>(code);
          } else if (code < 0x800) {
            result += static_cast<char>(0xC0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3F));
          } else if (code < 0x10000) {
            result += static_cast<char>(0xE0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
          } else {
            result += static_cast<char>(0xF0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
          }
          i = semi + 1;
          continue;
        } else {
          // Unknown entity — keep as-is
          result += text.substr(i, semi - i + 1);
        }
        i = semi + 1;
        continue;
      }
    }
    result += text[i];
    ++i;
  }
  return result;
}

static bool is_block_element(XmlStringView sv) {
  static const char* block_elements[] = {
      "p",     "h1",    "h2",     "h3",     "h4",     "h5",      "h6",      "li",         "ul",         "ol",
      "dl",    "dd",    "dt",     "tr",     "div",    "pre",     "nav",     "aside",      "table",      "tbody",
      "thead", "tfoot", "figure", "header", "footer", "section", "article", "blockquote", "figcaption",
  };
  for (const char* tag : block_elements) {
    if (sv == tag)
      return true;
  }
  return false;
}

static bool is_italic(XmlStringView name) {
  return name == "i" || name == "em";
}

static bool is_bold(XmlStringView name) {
  return name == "b" || name == "strong" || name == "h1" || name == "h2" || name == "h3" || name == "h4" ||
         name == "h5" || name == "h6";
}

static bool is_breaking(XmlStringView name) {
  return name == "br" || name == "tr";
}

static FontSize heading_size(XmlStringView name) {
  if (name == "h1" || name == "h2" || name == "h3")
    return FontSize::Large;
  return FontSize::Normal;
}

static bool is_small_element(XmlStringView name) {
  return name == "small" || name == "sub" || name == "sup";
}

class BodyParser {
 public:
  void push_text(const char* text, size_t len) {
    if (pre_depth_.has_value()) {
      push_preformatted_text(text, len);
      return;
    }

    std::string s(text, len);

    const char* t = s.c_str();
    size_t tlen = s.size();

    // If at start of paragraph, trim leading whitespace
    if (runs_.empty() && current_run_.empty()) {
      while (tlen > 0 && std::isspace(static_cast<unsigned char>(*t))) {
        ++t;
        --tlen;
      }
    }

    if (tlen == 0)
      return;

    bool starts_whitespace = std::isspace(static_cast<unsigned char>(t[0]));
    bool ends_whitespace = std::isspace(static_cast<unsigned char>(t[tlen - 1]));

    if (!has_trailing_space_ && starts_whitespace) {
      current_run_ += ' ';
    }

    // Normalize whitespace: collapse runs of whitespace to single space
    std::string decoded = decode_entities(std::string(t, tlen));
    bool in_space = false;
    std::string normalized;
    for (size_t i = 0; i < decoded.size(); ++i) {
      unsigned char uc = static_cast<unsigned char>(decoded[i]);
      // Treat UTF-8 non-breaking space (0xC2 0xA0) as whitespace
      bool is_sp = std::isspace(uc);
      if (!is_sp && uc == 0xC2 && i + 1 < decoded.size() && static_cast<unsigned char>(decoded[i + 1]) == 0xA0) {
        is_sp = true;
        ++i;  // skip the 0xA0 byte
      }
      if (is_sp) {
        if (!in_space) {
          normalized += ' ';
          in_space = true;
        }
      } else {
        normalized += decoded[i];
        in_space = false;
      }
    }

    // Trim leading/trailing whitespace from normalized
    size_t start = 0;
    while (start < normalized.size() && normalized[start] == ' ')
      ++start;
    size_t end = normalized.size();
    while (end > start && normalized[end - 1] == ' ')
      --end;

    if (start == end) {
      // All-whitespace text node: just note that it ended with space
      has_trailing_space_ = ends_whitespace;
      return;
    }

    current_run_ += apply_text_transform(normalized.substr(start, end - start));

    has_trailing_space_ = ends_whitespace;
    if (has_trailing_space_) {
      current_run_ += ' ';
    }
  }

  void set_bold(bool b) {
    if (bold_ != b) {
      flush_text(false);
      bold_ = b;
    }
  }

  void set_italic(bool i) {
    if (italic_ != i) {
      flush_text(false);
      italic_ = i;
    }
  }

  void set_size(FontSize s) {
    if (font_size_ != s) {
      flush_text(false);
      font_size_ = s;
    }
  }

  void set_vertical_align(VerticalAlign a) {
    if (vertical_align_ != a) {
      flush_text(false);
      vertical_align_ = a;
    }
  }

  void set_margin_left(uint16_t m) {
    if (margin_left_ != m) {
      flush_text(false);
      margin_left_ = m;
    }
  }

  void set_margin_right(uint16_t m) {
    if (margin_right_ != m) {
      flush_text(false);
      margin_right_ = m;
    }
  }

  void break_line(bool emit_if_empty = false) {
    flush_text(true);
    // If flush_text had nothing to flush (current_run_ was empty because
    // a prior set_size() already flushed it), mark the last run as breaking.
    if (!runs_.empty() && !runs_.back().breaking) {
      runs_.back().breaking = true;
    }
    // If we have no runs at all (e.g. <br/> between block elements),
    // emit an empty paragraph to produce visible blank space.
    // Only do this for actual <br/> tags (emit_if_empty=true), not <tr> etc.
    if (emit_if_empty && runs_.empty()) {
      auto para = Paragraph::make_text(TextParagraph{});
      paragraphs_.push_back(std::move(para));
    }
    // After a line break, suppress leading whitespace (from source formatting)
    has_trailing_space_ = true;
  }

  void flush_run() {
    flush_text(false);
    if (!runs_.empty()) {
      // Trim trailing whitespace from last run
      auto& last = runs_.back();
      while (!last.text.empty() && std::isspace(static_cast<unsigned char>(last.text.back()))) {
        last.text.pop_back();
      }
      TextParagraph tp;
      tp.runs = std::move(runs_);
      tp.alignment = alignment_;
      tp.indent = indent_;
      tp.inline_image = pending_inline_image_;  // attach float image if pending
      tp.line_height_pct = line_height_pct_;
      pending_inline_image_.reset();
      auto para = Paragraph::make_text(std::move(tp));

      // Apply vertical margin collapsing: max(pending_bottom, current_top)
      if (pending_margin_top_.has_value() || pending_margin_bottom_.has_value()) {
        uint16_t top = pending_margin_top_.value_or(0);
        uint16_t bot = pending_margin_bottom_.value_or(0);
        para.spacing_before = std::max(top, bot);
        pending_margin_top_.reset();
        pending_margin_bottom_.reset();
      }

      paragraphs_.push_back(std::move(para));
      runs_.clear();
      indent_.reset();
    } else if (pending_inline_image_.has_value()) {
      // No text to merge with — emit as standalone image paragraph
      auto img = *pending_inline_image_;
      pending_inline_image_.reset();
      paragraphs_.push_back(Paragraph::make_image(img.key, img.width, img.height));
    }
  }

  void push_image(uint16_t key, uint16_t w = 0, uint16_t h = 0) {
    flush_run();
    paragraphs_.push_back(Paragraph::make_image(key, w, h));
  }

  // Store a float image to be merged inline with the next text paragraph.
  void set_pending_inline_image(uint16_t key, uint16_t w, uint16_t h) {
    pending_inline_image_ = ImageRef(key, w, h);
  }

  void push_hr() {
    flush_run();
    paragraphs_.push_back(Paragraph::make_hr());
  }

  void push_page_break() {
    flush_run();
    // Only emit if there is already content (avoid leading blank page)
    if (!paragraphs_.empty()) {
      paragraphs_.push_back(Paragraph::make_page_break());
    }
  }

  std::vector<Paragraph> finish() {
    flush_run();
    return std::move(paragraphs_);
  }

  // Style stack management
  uint8_t depth = 0;
  struct BoolEntry {
    bool prev_value;
    uint8_t depth;
  };
  std::vector<BoolEntry> bold_stack_;
  std::vector<BoolEntry> italic_stack_;

  struct SizeEntry {
    FontSize prev_value;
    uint8_t depth;
  };
  std::vector<SizeEntry> size_stack_;

  struct VerticalAlignEntry {
    VerticalAlign prev_value;
    uint8_t depth;
  };
  std::vector<VerticalAlignEntry> vertical_align_stack_;
  VerticalAlign vertical_align_ = VerticalAlign::Baseline;

  std::optional<uint8_t> float_depth;
  std::optional<ImageRef> pending_inline_image_;  // float image awaiting merge
  std::optional<uint8_t> cell_depth;              // inside <td>/<th>: suppress inner block flushes
  std::optional<uint8_t> pre_depth_;              // inside <pre>: preserve whitespace
  bool merge_after_float_ = false;                // suppress next block flush after float close
  bool float_had_text_ = false;                   // true if text was pushed during a float
  struct AlignmentEntry {
    std::optional<Alignment> prev_value;
    uint8_t depth;
  };
  std::vector<AlignmentEntry> alignment_stack_;
  std::optional<Alignment> alignment_;
  std::optional<int16_t> indent_;

  // margin_left uses a stack for additive nesting (e.g. div.poem 30% + span 2em)
  struct MarginEntry {
    uint16_t prev_value;
    uint8_t depth;
  };
  std::vector<MarginEntry> margin_left_stack_;
  uint16_t margin_left_ = 0;
  std::vector<MarginEntry> margin_right_stack_;
  uint16_t margin_right_ = 0;

  // List tracking for bullet/number prefixes
  struct ListContext {
    bool ordered;      // true = <ol>, false = <ul>
    uint16_t counter;  // next item number (for <ol>)
    uint8_t depth;
  };
  std::vector<ListContext> list_stack_;

  // page-break-after tracking: emit a page break when these depths close
  std::vector<uint8_t> page_break_after_depths_;

  // Vertical margin collapsing: pending bottom margin from previous element
  std::optional<uint16_t> pending_margin_bottom_;
  std::optional<uint16_t> pending_margin_top_;  // margin-top for the NEXT paragraph emitted

  // Track margin-bottom values for elements, emit when they close
  struct MarginBottomEntry {
    uint16_t value;
    uint8_t depth;
  };
  std::vector<MarginBottomEntry> margin_bottom_stack_;

  // text-transform tracking
  struct TransformEntry {
    TextTransform prev_value;
    uint8_t depth;
  };
  std::vector<TransformEntry> transform_stack_;
  TextTransform text_transform_ = TextTransform::None;

  // line-height tracking (percentage of default, 100 = normal)
  struct LineHeightEntry {
    uint8_t prev_value;
    uint8_t depth;
  };
  std::vector<LineHeightEntry> line_height_stack_;
  uint8_t line_height_pct_ = 100;

  FontStyle style() const {
    if (bold_ && italic_)
      return FontStyle::BoldItalic;
    if (bold_)
      return FontStyle::Bold;
    if (italic_)
      return FontStyle::Italic;
    return FontStyle::Regular;
  }

  // Ensure a word boundary exists (space separator) without starting a new paragraph.
  // Used for table cells so adjacent <td> content doesn't concatenate.
  void ensure_word_break() {
    if (!current_run_.empty() && !std::isspace(static_cast<unsigned char>(current_run_.back()))) {
      current_run_ += ' ';
    }
    has_trailing_space_ = true;
  }

  void flush_text(bool breaking) {
    if (!current_run_.empty()) {
      Run r{std::move(current_run_), style(), font_size_, breaking};
      r.vertical_align = vertical_align_;
      r.margin_left = margin_left_;
      r.margin_right = margin_right_;
      runs_.push_back(std::move(r));
      current_run_.clear();
    }
  }

  std::string apply_text_transform(std::string s) const {
    if (text_transform_ == TextTransform::None)
      return s;
    if (text_transform_ == TextTransform::Uppercase) {
      for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    } else if (text_transform_ == TextTransform::Lowercase) {
      for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else if (text_transform_ == TextTransform::Capitalize) {
      bool after_space = true;
      for (auto& c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
          after_space = true;
        } else if (after_space) {
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          after_space = false;
        }
      }
    }
    return s;
  }

  void push_preformatted_text(const char* text, size_t len) {
    std::string decoded = decode_entities(std::string(text, len));
    // Split on newlines, emit each line as text with break_line() between
    size_t pos = 0;
    while (pos < decoded.size()) {
      size_t nl = decoded.find('\n', pos);
      if (nl == std::string::npos) {
        current_run_ += apply_text_transform(decoded.substr(pos));
        break;
      }
      if (nl > pos) {
        current_run_ += apply_text_transform(decoded.substr(pos, nl - pos));
      }
      break_line(true);
      pos = nl + 1;
    }
    has_trailing_space_ = false;
  }

  std::vector<Paragraph> paragraphs_;
  std::vector<Run> runs_;
  std::string current_run_;
  bool bold_ = false;
  bool italic_ = false;
  FontSize font_size_ = FontSize::Normal;
  bool has_trailing_space_ = false;
};

}  // anonymous namespace

EpubError parse_xhtml_body(const uint8_t* data, size_t size, const CssStylesheet* inline_css,
                           const CssStylesheet* extern_css, const std::string& base_dir, const ZipReader& zip,
                           std::vector<Paragraph>& out) {
  MemoryXmlInput input(data, size);
  // Use a buffer large enough for the full XHTML to avoid streaming boundary issues.
  std::vector<uint8_t> buf(std::max(size + 1, size_t(4096)));
  XmlReader reader;
  if (reader.open(input, buf.data(), buf.size()) != XmlError::Ok) {
    return EpubError::XmlError;
  }

  // Skip to <body>
  CssStylesheet parsed_inline_css(extern_css ? extern_css->config() : CssConfig{});
  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "style")) {
        // Parse inline stylesheet
        XmlEvent text;
        if (reader.next_event(text) == XmlError::Ok && text.type == XmlEventType::Text) {
          parsed_inline_css.extend_from_sheet(text.content.data, text.content.length);
        }
      } else if (sv_eq(ev.name, "body")) {
        break;
      }
    }
  }

  const CssStylesheet* effective_inline = inline_css ? inline_css : &parsed_inline_css;

  BodyParser parser;

  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::EndElement && sv_eq(ev.name, "body"))
      break;

    if (ev.type == XmlEventType::StartElement) {
      if (sv_eq(ev.name, "img") || sv_eq(ev.name, "image")) {
        const char* attr = sv_eq(ev.name, "img") ? "src" : "xlink:href";
        auto src = sv_to_string(ev.attrs.get(attr));
        bool found_image = false;
        if (!src.empty()) {
          // Read width/height attributes if present
          uint16_t img_w = 0, img_h = 0;
          auto w_sv = ev.attrs.get("width");
          auto h_sv = ev.attrs.get("height");
          if (!w_sv.empty()) {
            img_w = static_cast<uint16_t>(std::atoi(sv_to_string(w_sv).c_str()));
          }
          if (!h_sv.empty()) {
            img_h = static_cast<uint16_t>(std::atoi(sv_to_string(h_sv).c_str()));
          }
          std::string full_path = Epub::resolve_path(base_dir, src);
          for (size_t i = 0; i < zip.entry_count(); ++i) {
            if (zip.entry(i).name == full_path) {
              if (parser.float_depth.has_value()) {
                // Inside a float: store as pending inline image for text merge
                parser.set_pending_inline_image(static_cast<uint16_t>(i), img_w, img_h);
              } else {
                parser.push_image(static_cast<uint16_t>(i), img_w, img_h);
              }
              found_image = true;
              break;
            }
          }
        }
        // Fallback: use alt text if image not found (e.g. initial-cap floats)
        if (!found_image) {
          auto alt = sv_to_string(ev.attrs.get("alt"));
          if (!alt.empty()) {
            parser.push_text(alt.c_str(), alt.size());
          }
        }
        parser.depth++;  // balance synthetic EndElement for self-closing tag
        continue;
      }

      if (sv_eq(ev.name, "hr")) {
        parser.push_hr();
        parser.depth++;  // balance synthetic EndElement for self-closing tag
        continue;
      }

      // Get CSS rule (before block flush, so we can check for float)
      auto id_sv = ev.attrs.get("id");
      auto class_sv = ev.attrs.get("class");
      auto style_sv = ev.attrs.get("style");

      std::string id_str = sv_to_string(id_sv);
      std::string class_str = sv_to_string(class_sv);
      std::string name_str = sv_to_string(ev.name);

      CssRule inline_rule;
      if (!style_sv.empty()) {
        inline_rule = CssRule::parse(style_sv.data, style_sv.length, extern_css ? extern_css->config() : CssConfig{});
      }

      CssRule style = inline_rule +
                      effective_inline->get(name_str.c_str(), id_str.empty() ? nullptr : id_str.c_str(),
                                            class_str.empty() ? nullptr : class_str.c_str()) +
                      (extern_css ? extern_css->get(name_str.c_str(), id_str.empty() ? nullptr : id_str.c_str(),
                                                    class_str.empty() ? nullptr : class_str.c_str())
                                  : CssRule{});

      // Apply browser-default margins for elements that usually have them.
      // Only apply if no explicit margin-left was set by CSS.
      if (!style.margin_left.has_value()) {
        if (sv_eq(ev.name, "blockquote")) {
          style.margin_left = 36;  // ~3em default indent for blockquotes
        } else if (sv_eq(ev.name, "dd")) {
          style.margin_left = 24;  // ~2em default indent for definition descriptions
        } else if (sv_eq(ev.name, "ul") || sv_eq(ev.name, "ol")) {
          style.margin_left = 16;  // ~1.3em default indent for lists
        }
      }

      // figcaption defaults: small italic centered
      if (sv_eq(ev.name, "figcaption")) {
        if (!style.font_size.has_value())
          style.font_size = FontSize::Small;
        if (!style.italic.has_value())
          style.italic = true;
        if (!style.alignment.has_value())
          style.alignment = Alignment::Center;
      }
      // figure defaults: centered
      if (sv_eq(ev.name, "figure")) {
        if (!style.alignment.has_value())
          style.alignment = Alignment::Center;
      }

      bool is_floated = style.is_float.has_value() && *style.is_float;
      bool is_hidden = style.is_hidden.has_value() && *style.is_hidden;

      // Skip hidden elements entirely
      if (is_hidden) {
        // Skip all content until matching end element
        int skip_depth = 1;
        while (reader.next_event(ev) == XmlError::Ok) {
          if (ev.type == XmlEventType::EndOfFile)
            break;
          if (ev.type == XmlEventType::StartElement)
            ++skip_depth;
          else if (ev.type == XmlEventType::EndElement) {
            --skip_depth;
            if (skip_depth == 0)
              break;
          }
        }
        continue;
      }

      // Table cells: inject a space separator instead of a paragraph break
      // so that <tr> content stays on one line (e.g. TOC rows).
      if (sv_eq(ev.name, "td") || sv_eq(ev.name, "th")) {
        parser.ensure_word_break();
        parser.cell_depth = parser.depth + 1;  // will be incremented below
      }

      // page-break-before: always
      if (style.page_break_before.has_value() && *style.page_break_before) {
        parser.push_page_break();
      }

      // Float elements (e.g. .figleft initial-cap images) are treated as inline.
      // Suppress flush for ALL block boundaries after a float closes until text arrives.
      // Inside a table cell, suppress block flushes so inner divs don't split the row.
      if (is_block_element(ev.name) && !is_floated && !parser.float_depth.has_value() &&
          !parser.cell_depth.has_value()) {
        if (!parser.merge_after_float_) {
          parser.flush_run();
        }
      }

      parser.depth++;

      // page-break-after: track depth to emit on close
      if (style.page_break_after.has_value() && *style.page_break_after) {
        parser.page_break_after_depths_.push_back(parser.depth);
      }

      // Track list context for bullet/number prefixes
      if (sv_eq(ev.name, "ul")) {
        parser.list_stack_.push_back({false, 0, parser.depth});
      } else if (sv_eq(ev.name, "ol")) {
        uint16_t start_val = 1;
        auto start_sv = ev.attrs.get("start");
        if (!start_sv.empty()) {
          start_val =
              static_cast<uint16_t>(std::max(1, std::atoi(std::string(start_sv.data, start_sv.length).c_str())));
        }
        parser.list_stack_.push_back({true, start_val, parser.depth});
      } else if (sv_eq(ev.name, "li") && !parser.list_stack_.empty()) {
        auto& ctx = parser.list_stack_.back();
        if (ctx.ordered) {
          std::string prefix = std::to_string(ctx.counter) + ". ";
          parser.push_text(prefix.c_str(), prefix.size());
          ctx.counter++;
        } else {
          parser.push_text("\xe2\x80\xa2 ", 4);  // "• " (U+2022 bullet + space)
        }
      }

      if (is_bold(ev.name)) {
        if (!style.bold.has_value()) {
          parser.bold_stack_.push_back({parser.bold_, parser.depth});
          parser.set_bold(true);
        }
        FontSize hs = heading_size(ev.name);
        if (hs != FontSize::Normal) {
          parser.size_stack_.push_back({parser.font_size_, parser.depth});
          parser.set_size(hs);
        }
      } else if (is_italic(ev.name)) {
        if (!style.italic.has_value()) {
          parser.italic_stack_.push_back({parser.italic_, parser.depth});
          parser.set_italic(true);
        }
      } else if (is_small_element(ev.name)) {
        parser.size_stack_.push_back({parser.font_size_, parser.depth});
        parser.set_size(FontSize::Small);
        if (sv_eq(ev.name, "sup")) {
          parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
          parser.set_vertical_align(VerticalAlign::Super);
        } else if (sv_eq(ev.name, "sub")) {
          parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
          parser.set_vertical_align(VerticalAlign::Sub);
        }
      } else if (is_breaking(ev.name)) {
        parser.break_line(sv_eq(ev.name, "br"));
      }

      // <pre>: preserve whitespace and line breaks
      if (sv_eq(ev.name, "pre") && !parser.pre_depth_.has_value()) {
        parser.pre_depth_ = parser.depth;
      }

      if (style.italic.has_value()) {
        parser.italic_stack_.push_back({parser.italic_, parser.depth});
        parser.set_italic(*style.italic);
      }
      if (style.bold.has_value()) {
        parser.bold_stack_.push_back({parser.bold_, parser.depth});
        parser.set_bold(*style.bold);
      }
      if (style.font_size.has_value()) {
        parser.size_stack_.push_back({parser.font_size_, parser.depth});
        parser.set_size(*style.font_size);
      }
      if (style.alignment.has_value() && !parser.cell_depth.has_value()) {
        parser.alignment_stack_.push_back({parser.alignment_, parser.depth});
        parser.alignment_ = style.alignment;
      }
      if (style.indent.has_value() && !parser.cell_depth.has_value()) {
        parser.indent_ = style.indent;
      }
      if (style.margin_left.has_value() && !parser.cell_depth.has_value()) {
        parser.margin_left_stack_.push_back({parser.margin_left_, parser.depth});
        parser.set_margin_left(parser.margin_left_ + *style.margin_left);
      }
      if (style.margin_right.has_value() && !parser.cell_depth.has_value()) {
        parser.margin_right_stack_.push_back({parser.margin_right_, parser.depth});
        parser.set_margin_right(parser.margin_right_ + *style.margin_right);
      }
      if (style.margin_top.has_value() && !parser.cell_depth.has_value()) {
        uint16_t mt = *style.margin_top;
        // Collapse with any pending top margin (take max)
        if (parser.pending_margin_top_.has_value())
          parser.pending_margin_top_ = std::max(*parser.pending_margin_top_, mt);
        else
          parser.pending_margin_top_ = mt;
      }
      if (style.margin_bottom.has_value() && !parser.cell_depth.has_value()) {
        parser.margin_bottom_stack_.push_back({*style.margin_bottom, parser.depth});
      }
      if (style.text_transform.has_value()) {
        parser.transform_stack_.push_back({parser.text_transform_, parser.depth});
        parser.text_transform_ = *style.text_transform;
      }
      if (style.line_height_pct.has_value()) {
        parser.line_height_stack_.push_back({parser.line_height_pct_, parser.depth});
        parser.line_height_pct_ = *style.line_height_pct;
      }
      if (style.vertical_align.has_value()) {
        parser.vertical_align_stack_.push_back({parser.vertical_align_, parser.depth});
        parser.set_vertical_align(*style.vertical_align);
      }
      if (style.is_float.has_value() && *style.is_float) {
        // If we're already in merge-after-float mode (from a previous float),
        // flush pending content before starting a new float.
        // This prevents e.g. sidenote text from merging into a subsequent float's context.
        if (parser.merge_after_float_) {
          parser.flush_run();
          parser.merge_after_float_ = false;
        }
        parser.float_depth = parser.depth;
        parser.float_had_text_ = false;
      }

    } else if (ev.type == XmlEventType::EndElement) {
      if (is_block_element(ev.name) && !parser.float_depth.has_value() && !parser.merge_after_float_ &&
          !parser.cell_depth.has_value()) {
        parser.flush_run();
      }

      if (parser.depth > 0)
        parser.depth--;

      while (!parser.italic_stack_.empty() && parser.depth < parser.italic_stack_.back().depth) {
        parser.set_italic(parser.italic_stack_.back().prev_value);
        parser.italic_stack_.pop_back();
      }
      while (!parser.bold_stack_.empty() && parser.depth < parser.bold_stack_.back().depth) {
        parser.set_bold(parser.bold_stack_.back().prev_value);
        parser.bold_stack_.pop_back();
      }
      while (!parser.size_stack_.empty() && parser.depth < parser.size_stack_.back().depth) {
        parser.set_size(parser.size_stack_.back().prev_value);
        parser.size_stack_.pop_back();
      }
      while (!parser.vertical_align_stack_.empty() && parser.depth < parser.vertical_align_stack_.back().depth) {
        parser.set_vertical_align(parser.vertical_align_stack_.back().prev_value);
        parser.vertical_align_stack_.pop_back();
      }
      if (parser.float_depth.has_value() && parser.depth < *parser.float_depth) {
        parser.float_depth.reset();
        parser.has_trailing_space_ = false;
        // Only merge with next paragraph if this float had no text (image-only, like drop caps).
        // Floats with captions should flush as separate paragraphs.
        if (!parser.float_had_text_) {
          parser.merge_after_float_ = true;
        } else {
          parser.flush_run();
        }
      }
      if (parser.cell_depth.has_value() && parser.depth < *parser.cell_depth) {
        parser.cell_depth.reset();
      }
      if (parser.pre_depth_.has_value() && parser.depth < *parser.pre_depth_) {
        parser.pre_depth_.reset();
      }
      while (!parser.alignment_stack_.empty() && parser.depth < parser.alignment_stack_.back().depth) {
        parser.alignment_ = parser.alignment_stack_.back().prev_value;
        parser.alignment_stack_.pop_back();
      }
      while (!parser.margin_left_stack_.empty() && parser.depth < parser.margin_left_stack_.back().depth) {
        parser.set_margin_left(parser.margin_left_stack_.back().prev_value);
        parser.margin_left_stack_.pop_back();
      }
      while (!parser.margin_right_stack_.empty() && parser.depth < parser.margin_right_stack_.back().depth) {
        parser.set_margin_right(parser.margin_right_stack_.back().prev_value);
        parser.margin_right_stack_.pop_back();
      }
      while (!parser.list_stack_.empty() && parser.depth < parser.list_stack_.back().depth) {
        parser.list_stack_.pop_back();
      }
      while (!parser.transform_stack_.empty() && parser.depth < parser.transform_stack_.back().depth) {
        parser.text_transform_ = parser.transform_stack_.back().prev_value;
        parser.transform_stack_.pop_back();
      }
      while (!parser.line_height_stack_.empty() && parser.depth < parser.line_height_stack_.back().depth) {
        parser.line_height_pct_ = parser.line_height_stack_.back().prev_value;
        parser.line_height_stack_.pop_back();
      }
      while (!parser.page_break_after_depths_.empty() && parser.depth < parser.page_break_after_depths_.back()) {
        parser.page_break_after_depths_.pop_back();
        parser.push_page_break();
      }
      while (!parser.margin_bottom_stack_.empty() && parser.depth < parser.margin_bottom_stack_.back().depth) {
        uint16_t mb = parser.margin_bottom_stack_.back().value;
        parser.margin_bottom_stack_.pop_back();
        // Collapse: take max of pending bottom margins
        if (parser.pending_margin_bottom_.has_value())
          parser.pending_margin_bottom_ = std::max(*parser.pending_margin_bottom_, mb);
        else
          parser.pending_margin_bottom_ = mb;
      }

    } else if (ev.type == XmlEventType::Text) {
      // Skip whitespace-only text nodes inside floats and during merge-after-float
      if (parser.float_depth.has_value() || parser.merge_after_float_) {
        bool all_ws = true;
        for (size_t i = 0; i < ev.content.length; ++i) {
          if (!std::isspace(static_cast<unsigned char>(ev.content.data[i]))) {
            all_ws = false;
            break;
          }
        }
        if (all_ws)
          continue;
        // Non-whitespace text found: end merge-after-float mode
        parser.merge_after_float_ = false;
        if (parser.float_depth.has_value()) {
          parser.float_had_text_ = true;
        }
      }
      parser.push_text(ev.content.data, ev.content.length);
    }
  }

  out = parser.finish();
  return EpubError::Ok;
}

// ---------------------------------------------------------------------------
// Epub::parse_chapter
// ---------------------------------------------------------------------------

EpubError Epub::parse_chapter(IZipFile& file, size_t index, Chapter& out) const {
  if (index >= spine_.size())
    return EpubError::InvalidData;

  auto& spine_item = spine_[index];
  auto& entry = zip_.entry(spine_item.file_idx);

  // Find title from TOC
  out.title.reset();
  for (auto& toc_entry : toc_.entries) {
    if (toc_entry.file_idx == spine_item.file_idx) {
      out.title = toc_entry.label;
      break;
    }
  }

  // Extract chapter XHTML
  std::vector<uint8_t> data;
  if (zip_.extract(file, entry, data) != ZipError::Ok) {
    return EpubError::ZipError;
  }

  // Determine base directory
  std::string base_dir;
  auto slash = entry.name.rfind('/');
  if (slash != std::string::npos) {
    base_dir = entry.name.substr(0, slash + 1);
  }

  auto err = parse_xhtml_body(data.data(), data.size(), nullptr, &stylesheet_, base_dir, zip_, out.paragraphs);
  if (err != EpubError::Ok)
    return err;

  // Resolve image dimensions: many EPUBs omit width/height attributes on <img>.
  // Read actual dimensions from the image file header so layout can size them.
  for (auto& para : out.paragraphs) {
    if (para.type == ParagraphType::Image && para.image.width == 0 && para.image.height == 0) {
      if (para.image.key < zip_.entry_count()) {
        std::vector<uint8_t> img_data;
        if (zip_.extract(file, zip_.entry(para.image.key), img_data) == ZipError::Ok) {
          read_image_size(img_data.data(), img_data.size(), para.image.width, para.image.height);
        }
      }
    }
    // Also resolve inline float image dimensions
    if (para.type == ParagraphType::Text && para.text.inline_image.has_value()) {
      auto& img = *para.text.inline_image;
      if (img.width == 0 && img.height == 0 && img.key < zip_.entry_count()) {
        std::vector<uint8_t> img_data;
        if (zip_.extract(file, zip_.entry(img.key), img_data) == ZipError::Ok) {
          read_image_size(img_data.data(), img_data.size(), img.width, img.height);
        }
      }
    }
  }

  // Promote large inline float images to standalone Image paragraphs.
  // Large images (wider than 1/3 content width or taller than 4 line heights)
  // can't be rendered inline — insert them as a separate paragraph before the text.
  const uint16_t content_w = stylesheet_.config().content_width;
  for (size_t i = 0; i < out.paragraphs.size(); ++i) {
    auto& para = out.paragraphs[i];
    if (para.type == ParagraphType::Text && para.text.inline_image.has_value()) {
      const auto& img = *para.text.inline_image;
      if (img.width > content_w / 3 || img.height > 120) {
        // Extract and insert as standalone image paragraph
        auto img_para = Paragraph::make_image(img.key, img.width, img.height);
        para.text.inline_image.reset();
        out.paragraphs.insert(out.paragraphs.begin() + static_cast<ptrdiff_t>(i), std::move(img_para));
        ++i;  // skip past the newly inserted image
      }
    }
  }

  return EpubError::Ok;
}

}  // namespace microreader
