#pragma once

#include <iostream>
#include <string>

#include "microreader/core/Log.h"

class DesktopLogger final : public microreader::ILogger {
 public:
  void log(microreader::LogLevel level, const std::string& message) override {
    switch (level) {
      case microreader::LogLevel::Info:
        std::cout << "[log][info] " << message << std::endl;
        break;
      case microreader::LogLevel::Warning:
        std::cout << "[log][warn] " << message << std::endl;
        break;
      case microreader::LogLevel::Error:
        std::cerr << "[log][error] " << message << std::endl;
        break;
    }
  }
};
