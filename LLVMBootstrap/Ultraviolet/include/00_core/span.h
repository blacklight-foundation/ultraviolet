#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "00_core/source_text.h"

namespace ultraviolet::core {

struct SourceLocation {
  std::string file;
  std::size_t offset = 0;
  std::size_t line = 0;
  std::size_t column = 0;
};

struct Span {
  std::string file;
  std::size_t start_offset = 0;
  std::size_t end_offset = 0;
  std::size_t start_line = 0;
  std::size_t start_col = 0;
  std::size_t end_line = 0;
  std::size_t end_col = 0;
};

std::pair<std::size_t, std::size_t> SpanRange(const Span& sp);

SourceLocation Locate(const SourceFile& source, std::size_t offset);

std::pair<std::size_t, std::size_t> ClampSpan(
    const SourceFile& source,
    std::size_t start,
    std::size_t end);

Span SpanOf(const SourceFile& source, std::size_t start, std::size_t end);

}  // namespace ultraviolet::core
