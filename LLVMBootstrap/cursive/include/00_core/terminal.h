#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace cursive::core {

// Color codes for terminal output
enum class Color {
  Reset,
  Red,
  Green,
  Yellow,
  Blue,
  Magenta,
  Cyan,
  White,
  BoldRed,
  BoldGreen,
  BoldYellow,
  BoldBlue,
  BoldMagenta,
  BoldCyan,
  BoldWhite,
};

// Check if color output is enabled for the given stream.
// Returns false if the NO_COLOR env var is set or the stream is not a TTY.
bool IsColorEnabled(FILE* stream);

// Override mode for color output.
enum class ColorOverride {
  Auto,      // Delegate to IsColorEnabled()
  ForceOn,   // Always enable color
  ForceOff,  // Always disable color
};

// Check if color is enabled, respecting the override mode.
bool IsColorEnabledWithOverride(FILE* stream, ColorOverride override_mode);

// Returns terminal width in columns, or 0 if unknown.
int TerminalWidth();

// Return the ANSI escape code for a given color.
// If enabled is false, returns an empty string.
std::string_view ColorCode(Color color, bool enabled);

// Wrap text in ANSI color codes. If enabled is false, returns text unchanged.
std::string Colorize(std::string_view text, Color color, bool enabled);

}  // namespace cursive::core
