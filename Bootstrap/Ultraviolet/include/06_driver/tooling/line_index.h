#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "00_core/span.h"

namespace ultraviolet::driver::tooling {

struct LinePosition {
  std::size_t line = 0;
  std::size_t character = 0;
};

struct LineRange {
  LinePosition start;
  LinePosition end;
};

class LineIndex {
 public:
  LineIndex() = default;
  explicit LineIndex(std::string text);

  const std::string& text() const { return text_; }
  std::size_t ByteOffsetAt(LinePosition position) const;
  LinePosition PositionAt(std::size_t byte_offset) const;
  LineRange RangeFor(const core::Span& span) const;
  std::size_t Utf16Length(std::size_t start_byte,
                          std::size_t end_byte) const;

 private:
  std::string text_;
  std::vector<std::size_t> line_starts_;
};

}  // namespace ultraviolet::driver::tooling
