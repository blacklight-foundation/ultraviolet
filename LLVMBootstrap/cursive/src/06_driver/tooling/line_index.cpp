#include "06_driver/tooling/line_index.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace cursive::driver::tooling {

namespace {

std::uint32_t DecodeOne(std::string_view text, std::size_t& offset) {
  const unsigned char first = static_cast<unsigned char>(text[offset]);
  if (first <= 0x7F) {
    ++offset;
    return first;
  }
  if ((first & 0xE0) == 0xC0 && offset + 1 < text.size()) {
    const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    offset += 2;
    return static_cast<std::uint32_t>(((first & 0x1F) << 6) | (b1 & 0x3F));
  }
  if ((first & 0xF0) == 0xE0 && offset + 2 < text.size()) {
    const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
    offset += 3;
    return static_cast<std::uint32_t>(((first & 0x0F) << 12) |
                                      ((b1 & 0x3F) << 6) | (b2 & 0x3F));
  }
  if ((first & 0xF8) == 0xF0 && offset + 3 < text.size()) {
    const unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    const unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
    const unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
    offset += 4;
    return static_cast<std::uint32_t>(((first & 0x07) << 18) |
                                      ((b1 & 0x3F) << 12) |
                                      ((b2 & 0x3F) << 6) | (b3 & 0x3F));
  }
  ++offset;
  return first;
}

std::size_t Utf16Units(std::uint32_t scalar) {
  return scalar > 0xFFFF ? 2 : 1;
}

}  // namespace

LineIndex::LineIndex(std::string text) : text_(std::move(text)) {
  line_starts_.push_back(0);
  for (std::size_t i = 0; i < text_.size(); ++i) {
    if (text_[i] == '\n') {
      line_starts_.push_back(i + 1);
    }
  }
}

std::size_t LineIndex::ByteOffsetAt(LinePosition position) const {
  if (line_starts_.empty()) {
    return 0;
  }
  const std::size_t line =
      std::min(position.line, line_starts_.size() - 1);
  const std::size_t start = line_starts_[line];
  const std::size_t end =
      (line + 1 < line_starts_.size()) ? line_starts_[line + 1] : text_.size();
  std::size_t offset = start;
  std::size_t utf16 = 0;
  while (offset < end && offset < text_.size() && text_[offset] != '\n') {
    const std::size_t before = offset;
    const std::uint32_t scalar = DecodeOne(text_, offset);
    const std::size_t next_utf16 = utf16 + Utf16Units(scalar);
    if (next_utf16 > position.character) {
      return before;
    }
    utf16 = next_utf16;
    if (utf16 >= position.character) {
      return offset;
    }
  }
  return std::min(offset, text_.size());
}

LinePosition LineIndex::PositionAt(std::size_t byte_offset) const {
  if (line_starts_.empty()) {
    return {};
  }
  const std::size_t clamped = std::min(byte_offset, text_.size());
  auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), clamped);
  std::size_t line = 0;
  if (it != line_starts_.begin()) {
    line = static_cast<std::size_t>(it - line_starts_.begin() - 1);
  }
  const std::size_t start = line_starts_[line];
  return LinePosition{line, Utf16Length(start, clamped)};
}

LineRange LineIndex::RangeFor(const core::Span& span) const {
  return LineRange{PositionAt(span.start_offset), PositionAt(span.end_offset)};
}

std::size_t LineIndex::Utf16Length(std::size_t start_byte,
                                   std::size_t end_byte) const {
  std::size_t offset = std::min(start_byte, text_.size());
  const std::size_t end = std::min(end_byte, text_.size());
  std::size_t units = 0;
  while (offset < end) {
    const std::size_t before = offset;
    const std::uint32_t scalar = DecodeOne(text_, offset);
    if (offset > end) {
      offset = before;
      break;
    }
    units += Utf16Units(scalar);
  }
  return units;
}

}  // namespace cursive::driver::tooling
