#include "Application.h"

#include <cstdlib>
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

void Application::start(DrawBuffer& buf) {
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
    menu_.set_reader_font(reader_font_);

  // Set up settings file path if data_dir_ is set
  if (data_dir_) {
    settings_path_ = std::string(data_dir_) + "/settings";
  }

  screen_mgr_.push(&menu_, buf);

  // Try to restore last screen/book from settings
  std::string last_screen, last_book_path;
  bool restored = false;
  if (!settings_path_.empty() && load_settings_(last_screen, last_book_path)) {
    if (last_screen == "reader" && !last_book_path.empty()) {
      auto_open_book(last_book_path.c_str(), buf);
      restored = true;
    }
  }

  buf.full_refresh();
}

void Application::auto_open_book(const char* epub_path, DrawBuffer& buf) {
  // Use the MainMenu's ReaderScreen instance for all book opens
  ReaderScreen* reader = menu_.reader();
  reader->set_path(epub_path);
  if (reader_font_)
    reader->set_fonts(reader_font_);

  screen_mgr_.push(reader, buf);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime) {
  if (!started_)
    start(buf);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    // Stop the active screen so it can save state (e.g. reading position).
    if (IScreen* top = screen_mgr_.top())
      top->stop();

    // Save last screen and book info
    save_settings_();

    // draw sleeping screen before powering down
    buf.draw_image(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT);

    // add sleeping... text below the image
    const char* sleep_text = "sleeping...";
    buf.draw_text_centered(DrawBuffer::kWidth / 2, DrawBuffer::kHeight - 24, sleep_text, true);

    buf.full_refresh(RefreshMode::Full, true);  // turnOffScreen=true to power down immediately after refresh
    buf.deep_sleep();
    running_ = false;
    return;
  }

  IScreen* top = screen_mgr_.top();
  if (top) {
    if (!top->update(buttons_, buf, runtime)) {
      // Screen signalled exit.
      if (top == &menu_) {
        // Menu chose a sub-screen — push it.
        IScreen* chosen = menu_.chosen();
        if (chosen) {
          screen_mgr_.push(chosen, buf);
          buf.refresh();
        }
      } else {
        // Check if the exiting screen wants to replace itself or push a sub-screen.
        IScreen* replace = top->replace_with();
        IScreen* next = top->chosen();

        if (replace) {
          // Pop the current screen then push the replacement.
          screen_mgr_.pop(buf);
          screen_mgr_.push(replace, buf);
          buf.refresh();
        } else if (next) {
          screen_mgr_.push(next, buf);
          buf.refresh();
        } else {
          // Pop back to the previous screen.
          screen_mgr_.pop(buf);
          buf.refresh();
        }
      }
    }
  }

}  // namespace microreader

// --- Place these outside the namespace to avoid qualified name errors ---
void microreader::Application::save_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "w");
  if (!f)
    return;
  // Only two screens are relevant for persistence: menu and reader
  IScreen* top = screen_mgr_.top();
  std::string screen_type = "menu";
  std::string book_path;

  ReaderScreen* reader = menu_.reader();
  if (top == reader) {
    screen_type = "reader";
    // Save the current book path if possible
    if (reader && reader->has_path()) {
      book_path = reader->get_path();
    }
  }
  std::fprintf(f, "%s\n%s\n", screen_type.c_str(), book_path.c_str());
  std::fflush(f);
  std::fclose(f);
}

bool microreader::Application::load_settings_(std::string& last_screen, std::string& last_book_path) {
  if (settings_path_.empty())
    return false;
  FILE* f = std::fopen(settings_path_.c_str(), "r");
  if (!f)
    return false;
  char screen_buf[32] = {0};
  char book_buf[512] = {0};
  bool ok = false;
  if (std::fgets(screen_buf, sizeof(screen_buf), f)) {
    // Remove trailing newline
    char* nl = std::strchr(screen_buf, '\n');
    if (nl)
      *nl = 0;
    last_screen = screen_buf;
    if (std::fgets(book_buf, sizeof(book_buf), f)) {
      nl = std::strchr(book_buf, '\n');
      if (nl)
        *nl = 0;
      last_book_path = book_buf;
    }
    ok = true;
  }
  std::fclose(f);
  return ok;
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
