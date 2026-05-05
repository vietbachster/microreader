#include "Application.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HeapLog.h"
#include "microreader/resources/bebop_image.h"

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace microreader {

const char* Application::build_info() const {
  return "microreader";
}

void Application::start(DrawBuffer& buf, IRuntime& runtime) {
  ticks_ = 0;
  uptime_ms_ = 0;
  buttons_ = ButtonState{};
  started_ = true;
  running_ = true;
  MR_LOGI("app", "%s", build_info());
#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  if (reader_font_)
    reader_.set_fonts(reader_font_);

  menu_.set_app(this);
  reader_.set_app(this);
  settings_.set_app(this);
  bouncing_ball_.set_app(this);
  grayscale_demo_.set_app(this);
  reader_options_.set_app(this);
  chapter_select_.set_app(this);

  // Set up settings file path if data_dir_ is set
  if (data_dir_)
    settings_path_ = std::string(data_dir_) + "/settings";

  // Load settings first so initial_selection_ and reader settings are ready
  // before the menu's on_start() (directory scan + selection restore) runs.
  load_settings_();

  screen_mgr_.push(&menu_, buf, runtime);

  // Auto-open last book if one was active at shutdown — but only if the
  // reader font is valid.  Without a font the reader cannot render text, so
  // fall back to the main menu and let the normal pre-book-open hook install
  // the font on the first book open.
  if (!pending_book_path_.empty()) {
    if (reader_font_ && reader_font_->valid()) {
      auto_open_book(pending_book_path_.c_str(), buf, runtime);
    } else {
      MR_LOGI("app", "skipping auto-open (no valid font) — starting from main menu");
    }
    pending_book_path_.clear();
  }

  buf.full_refresh();
}

void Application::auto_open_book(const char* epub_path, DrawBuffer& buf, IRuntime& runtime) {
  reader_.set_path(epub_path);
  if (reader_font_)
    reader_.set_fonts(reader_font_);

  screen_mgr_.push(&reader_, buf, runtime);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime) {
  if (!started_)
    start(buf, runtime);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    // Stop the active screen so it can save state (e.g. reading position).
    if (IScreen* top = screen_mgr_.top())
      top->stop();

    // Save all persistent settings
    save_settings_();

    // draw sleeping screen before powering down
    buf.draw_image(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT);

    // add sleeping... text below the image
    const char* sleep_text = "sleeping...";
    buf.draw_text_centered(DrawBuffer::kWidth / 2, DrawBuffer::kHeight - 24, sleep_text, true);

    buf.full_refresh(RefreshMode::Full, false);

    // Grayscale pass using the pre-built LSB/MSB planes of the bebop image,
    // then power off the display.
    buf.show_grayscale_image(bebop_image_lsb, bebop_image_msb, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT);

    running_ = false;
    return;
  }

  IScreen* top = screen_mgr_.top();
  if (top) {
    top->update(buttons_, buf, runtime);

    // Process pending navigation (queued by screens via push_screen/replace_screen).
    if (pending_replace_ != ScreenId::None) {
      ScreenId id = pending_replace_;
      pending_replace_ = ScreenId::None;
      screen_mgr_.pop(buf, runtime);
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
    } else if (pending_push_ != ScreenId::None) {
      ScreenId id = pending_push_;
      pending_push_ = ScreenId::None;
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
    } else if (pending_pop_count_ > 0) {
      int count = pending_pop_count_;
      pending_pop_count_ = 0;
      if (top == &reader_)
        save_settings_();
      screen_mgr_.pop(count, buf, runtime);
      buf.refresh();
    }
  }
}  // namespace microreader

IScreen* microreader::Application::screen_for_(ScreenId id) {
  switch (id) {
    case ScreenId::MainMenu:
      return &menu_;
    case ScreenId::Reader:
      return &reader_;
    case ScreenId::Settings:
      return &settings_;
    case ScreenId::BouncingBall:
      return &bouncing_ball_;
    case ScreenId::GrayscaleDemo:
      return &grayscale_demo_;
    case ScreenId::ReaderOptions:
      return &reader_options_;
    case ScreenId::ChapterSelect:
      return &chapter_select_;
    default:
      return nullptr;
  }
}
void microreader::Application::save_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "w");
  if (!f)
    return;

  // Version tag
  std::fprintf(f, "v=1\n");

  // Last screen / book — treat reader-is-anywhere-in-stack as "reader" so
  // shutting down from ReaderOptionsScreen still boots back into the reader.
  ReaderScreen* reader = &reader_;
  const bool reader_active = screen_mgr_.contains(reader);
  std::fprintf(f, "screen=%s\n", reader_active ? "reader" : "menu");
  if (reader_active && reader->has_path())
    std::fprintf(f, "book_path=%s\n", reader->get_path().c_str());

  // Last book-list selection: prefer the currently highlighted entry so
  // power-off while browsing still saves position; fall back to last opened.
  const std::string& sel =
      !menu_.current_book_path().empty() ? menu_.current_book_path() : menu_.last_selected_book_path();
  if (!sel.empty())
    std::fprintf(f, "book_sel=%s\n", sel.c_str());

  // Reader display settings
  const ReaderSettings& rs = reader->reader_settings();
  std::fprintf(f, "align_override=%u\n", static_cast<unsigned>(rs.align_override));
  std::fprintf(f, "padding_h=%u\n", static_cast<unsigned>(rs.padding_h_idx));
  std::fprintf(f, "padding_v=%u\n", static_cast<unsigned>(rs.padding_v_idx));
  std::fprintf(f, "spacing_override=%u\n", static_cast<unsigned>(rs.spacing_override));
  std::fprintf(f, "progress=%u\n", static_cast<unsigned>(rs.progress_style));

  // Menu list format
  std::fprintf(f, "list_format=%u\n", static_cast<unsigned>(menu_.list_format()));

  std::fclose(f);
}

void microreader::Application::load_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "r");
  if (!f)
    return;

  char line[512];
  std::string last_screen, last_book_path, book_sel;
  ReaderSettings& rs = reader_.reader_settings();

  while (std::fgets(line, sizeof(line), f)) {
    // Strip trailing newline
    char* nl = std::strchr(line, '\n');
    if (nl)
      *nl = 0;

    char sval[512];
    unsigned uval = 0;
    if (std::sscanf(line, "screen=%511s", sval) == 1)
      last_screen = sval;
    else if (std::sscanf(line, "book_path=%511[^\n]", sval) == 1)
      last_book_path = sval;
    else if (std::sscanf(line, "book_sel=%511[^\n]", sval) == 1)
      book_sel = sval;
    else if (std::sscanf(line, "align_override=%u", &uval) == 1)
      rs.align_override =
          uval < ReaderSettings::kNumAlignPresets ? static_cast<AlignOverride>(uval) : AlignOverride::Book;
    else if (std::sscanf(line, "justify=%u", &uval) == 1)  // Backwards compatibility
      rs.align_override = uval != 0 ? AlignOverride::Justify : AlignOverride::Left;
    else if (std::sscanf(line, "padding_h=%u", &uval) == 1)
      rs.padding_h_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "padding_v=%u", &uval) == 1)
      rs.padding_v_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "spacing_override=%u", &uval) == 1)
      rs.spacing_override = uval < ReaderSettings::kNumSpacingPresets ? static_cast<SpacingOverride>(uval)
                                                                      : SpacingOverride::Spacing_1_0x;
    else if (std::sscanf(line, "line_spacing=%u", &uval) == 1)  // Backwards compatibility
      rs.spacing_override = SpacingOverride::Book;
    else if (std::sscanf(line, "progress=%u", &uval) == 1)
      rs.progress_style = uval <= 2 ? static_cast<ProgressStyle>(uval) : ProgressStyle::Bar;
    else if (std::sscanf(line, "list_format=%u", &uval) == 1)
      menu_.set_list_format(uval <= 2 ? static_cast<BookListFormat>(uval) : BookListFormat::TitleAndAuthor);
  }
  std::fclose(f);
  MR_LOGI("app", "Loaded settings: align=%u ph=%u pv=%u ls=%u prog=%u sel=%s", static_cast<unsigned>(rs.align_override),
          rs.padding_h_idx, rs.padding_v_idx, static_cast<unsigned>(rs.spacing_override),
          static_cast<unsigned>(rs.progress_style), book_sel.c_str());

  // Restore book list selection highlight
  if (!book_sel.empty())
    menu_.set_initial_selection(book_sel.c_str());

  // Store the book to auto-open; actual push happens in start() after buf is ready.
  if (last_screen == "reader" && !last_book_path.empty())
    pending_book_path_ = last_book_path;
}

bool Application::running() const {
  return running_;
}
uint64_t Application::tick_count() const {
  return ticks_;
}
uint32_t Application::uptime_ms() const {
  return uptime_ms_;
}

}  // namespace microreader
