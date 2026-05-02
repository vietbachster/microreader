#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ContentModel.h"

namespace microreader {

enum class TextTransform : uint8_t { None = 0, Uppercase, Lowercase, Capitalize };

// Configuration for resolving relative CSS units (em, %) to pixels.
struct CssConfig {
  uint16_t glyph_width = 12;     // pixels per em (font glyph width)
  uint16_t content_width = 440;  // content area width for % calculations
  uint16_t max_margin_pct = 15;  // max combined margin-left+right as % of content_width
};

// A CSS rule: the style properties we care about for e-book rendering.
struct CssRule {
  std::optional<Alignment> alignment;
  std::optional<bool> italic;
  std::optional<bool> bold;
  std::optional<int16_t> indent;
  std::optional<uint8_t> font_size_pct;
  std::optional<uint16_t> margin_left;
  std::optional<uint16_t> margin_right;
  std::optional<uint16_t> margin_top;
  std::optional<uint16_t> margin_bottom;
  std::optional<bool> is_float;           // true if float: left|right
  std::optional<bool> is_hidden;          // true if display: none
  std::optional<bool> page_break_before;  // true if page-break-before: always|left|right
  std::optional<bool> page_break_after;   // true if page-break-after: always|left|right
  std::optional<TextTransform> text_transform;
  std::optional<VerticalAlign> vertical_align;
  std::optional<uint8_t> line_height_pct;       // line-height as % of default (100 = normal)
  std::optional<bool> list_style_none;          // true if list-style-type: none
  std::optional<bool> font_variant_small_caps;  // true if font-variant: small-caps
  std::optional<bool> border_top;               // true if border-top is visible (solid/dashed/etc.)

  bool has_any() const {
    return alignment.has_value() || italic.has_value() || bold.has_value() || indent.has_value() ||
           font_size_pct.has_value() || margin_left.has_value() || margin_right.has_value() || margin_top.has_value() ||
           margin_bottom.has_value() || is_float.has_value() || is_hidden.has_value() ||
           page_break_before.has_value() || page_break_after.has_value() || text_transform.has_value() ||
           vertical_align.has_value() || line_height_pct.has_value() || list_style_none.has_value() ||
           font_variant_small_caps.has_value() || border_top.has_value();
  }

  // Parse declarations like "text-align: center; font-weight: bold"
  static CssRule parse(const char* declarations, size_t length, const CssConfig& config = CssConfig{});
  static CssRule parse(const std::string& s, const CssConfig& config = CssConfig{}) {
    return parse(s.c_str(), s.size(), config);
  }

  // Merge: rhs overrides lhs where present
  CssRule operator+(const CssRule& rhs) const;
};

// A simple CSS stylesheet. Matches selectors by element name, id, and class.
class CssStylesheet {
 public:
  CssStylesheet() = default;
  explicit CssStylesheet(const CssConfig& config) : config_(config) {}

  void set_config(const CssConfig& config) {
    config_ = config;
  }
  const CssConfig& config() const {
    return config_;
  }

  // Parse and add rules from a CSS string (contents of a <style> block or .css file).
  void extend_from_sheet(const char* css, size_t length);
  void extend_from_sheet(const std::string& s) {
    extend_from_sheet(s.c_str(), s.size());
  }

  // Look up cascaded style for an element.
  CssRule get(const char* element, const char* id, const char* cls) const;
  // Overload accepting lengths (avoids null-termination requirement).
  CssRule get(const char* element, size_t element_len, const char* id, size_t id_len, const char* cls,
              size_t cls_len) const;

  size_t rule_count() const {
    return rules_.size();
  }

  // Internal selector type (public for inline matching in .cpp)
  struct Selector {
    std::string element;
    std::string id;
    std::vector<std::string> classes;

    static bool try_parse(const char* s, size_t len, Selector& out);
    bool matches(const char* element, const char* id, const std::vector<std::string>& classes) const;
    uint32_t specificity() const;
  };

 private:
  CssConfig config_;
  std::vector<std::pair<Selector, CssRule>> rules_;

  static std::string filter_comments(const char* css, size_t length);
};

}  // namespace microreader
