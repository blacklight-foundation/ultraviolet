#include "00_core/terminal.h"
#include "00_core/host/services.h"

#include <cstdlib>

namespace ultraviolet::core {

bool IsColorEnabled(FILE* stream) {
  // NO_COLOR convention: https://no-color.org/
  if (const auto no_color = HostGetEnvUtf8("NO_COLOR");
      no_color.has_value()) {
    return false;
  }
  return QueryHostTerminal(stream).ansi_enabled;
}

bool IsColorEnabledWithOverride(FILE* stream, ColorOverride override_mode) {
  switch (override_mode) {
    case ColorOverride::ForceOn:
      return true;
    case ColorOverride::ForceOff:
      return false;
    case ColorOverride::Auto:
      return IsColorEnabled(stream);
  }
  return IsColorEnabled(stream);
}

int TerminalWidth() {
  const HostTerminalInfo info = QueryHostTerminal(stderr);
  if (info.width > 0) {
    return info.width;
  }
  // Fallback: check COLUMNS environment variable
  if (const auto columns = HostGetEnvUtf8("COLUMNS");
      columns.has_value() && !columns->empty()) {
    int val = std::atoi(columns->c_str());
    if (val > 0) {
      return val;
    }
  }
  return 0;
}

std::string_view ColorCode(Color color, bool enabled) {
  if (!enabled) {
    return "";
  }
  switch (color) {
    case Color::Reset:
      return "\033[0m";
    case Color::Red:
      return "\033[31m";
    case Color::Green:
      return "\033[32m";
    case Color::Yellow:
      return "\033[33m";
    case Color::Blue:
      return "\033[34m";
    case Color::Magenta:
      return "\033[35m";
    case Color::Cyan:
      return "\033[36m";
    case Color::White:
      return "\033[37m";
    case Color::BoldRed:
      return "\033[1;31m";
    case Color::BoldGreen:
      return "\033[1;32m";
    case Color::BoldYellow:
      return "\033[1;33m";
    case Color::BoldBlue:
      return "\033[1;34m";
    case Color::BoldMagenta:
      return "\033[1;35m";
    case Color::BoldCyan:
      return "\033[1;36m";
    case Color::BoldWhite:
      return "\033[1;37m";
  }
  return "";
}

std::string Colorize(std::string_view text, Color color, bool enabled) {
  if (!enabled) {
    return std::string(text);
  }
  std::string out;
  const auto code = ColorCode(color, true);
  const auto reset = ColorCode(Color::Reset, true);
  out.reserve(code.size() + text.size() + reset.size());
  out.append(code);
  out.append(text);
  out.append(reset);
  return out;
}

}  // namespace ultraviolet::core
