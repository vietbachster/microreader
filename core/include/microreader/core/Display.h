#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace microreader {

enum class RefreshMode {
  Full,
  Fast
};

struct DisplayFrame {
  static constexpr std::size_t kMaxLines = 8;
  static constexpr std::size_t kMaxLineLength = 96;

  std::array<std::array<char, kMaxLineLength>, kMaxLines> lines{};
  std::size_t line_count = 0;
};

class IDisplay {
 public:
  virtual ~IDisplay() = default;

  virtual void present(const DisplayFrame& frame, RefreshMode mode) = 0;
};

}  // namespace microreader
