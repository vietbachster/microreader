#include <filesystem>
#include <iostream>

#include "display.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/Loop.h"
#include "microreader/display/DrawBuffer.h"
#include "runtime.h"

int main() {
  try {
    DesktopRuntime runtime(16);
    DesktopInputSource input(runtime);
    DesktopEmulatorDisplay display(runtime);
    microreader::Application app;
    microreader::DrawBuffer buf(display);

    // Mount sd/books as the virtual SD card books directory.
    static std::string books_path = std::filesystem::absolute("sd/books").string();
    std::filesystem::create_directories(books_path);
    app.set_books_dir(books_path.c_str());

    app.start(buf);
    microreader::run_loop(app, buf, input, runtime);
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
