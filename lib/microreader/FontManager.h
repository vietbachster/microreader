#pragma once

#include "content/BitmapFont.h"
#include "display/DrawBuffer.h"

namespace microreader {

// Owns the BitmapFont slots and BitmapFontSet for one platform.
// Desktop uses this directly (ensure_ready is a no-op).
// ESP32 subclasses it to override ensure_ready with partition provisioning.
class FontManager {
 public:
  // Init one font slot. The data pointer must outlive this object.
  void load_font(const uint8_t* data, size_t sz) {
    if (num_fonts_ >= kMaxFonts)
      return;
    int idx = num_fonts_++;
    prop_fonts_[idx].init(data, sz);
    font_set_.add(&prop_fonts_[idx]);
  }

  // Return the font set (use with app.set_reader_font()).
  BitmapFontSet* font_set() {
    return font_set_.valid() ? &font_set_ : nullptr;
  }

  bool valid() const {
    return font_set_.valid();
  }

  // No provisioning needed on desktop.
  virtual void ensure_ready(DrawBuffer&) {}

 protected:
  BitmapFont prop_fonts_[kMaxFonts];
  BitmapFontSet font_set_;
  int num_fonts_ = 0;
};

}  // namespace microreader
