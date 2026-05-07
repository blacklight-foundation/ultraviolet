#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/diagnostics.h"
#include "00_core/source_text.h"

namespace cursive::core {

struct SourceLoadStartState {
  std::string path;
  std::vector<std::uint8_t> bytes;
};

struct SourceLoadSizedState {
  std::string path;
  std::vector<std::uint8_t> bytes;
};

struct SourceLoadDecodedState {
  std::string path;
  std::vector<std::uint8_t> bytes;
  Scalars scalars;
};

struct SourceLoadBomStrippedState {
  std::string path;
  std::vector<std::uint8_t> bytes;
  Scalars scalars;
  bool had_bom = false;
  std::optional<std::size_t> j;
};

struct SourceLoadNormalizedState {
  std::string path;
  std::vector<std::uint8_t> bytes;
  Scalars scalars;
  std::optional<std::size_t> j;
};

struct SourceLoadLineMappedState {
  std::string path;
  std::vector<std::uint8_t> bytes;
  Scalars scalars;
  std::vector<std::size_t> line_starts;
};

struct SourceLoadValidatedState {
  SourceFile source;
};

struct SourceLoadErrorState {
  std::string code;
};

using SourceLoadState = std::variant<
    SourceLoadStartState,
    SourceLoadSizedState,
    SourceLoadDecodedState,
    SourceLoadBomStrippedState,
    SourceLoadNormalizedState,
    SourceLoadLineMappedState,
    SourceLoadValidatedState,
    SourceLoadErrorState>;

struct SourceLoadResult {
  std::optional<SourceFile> source;
  DiagnosticStream diags;
};

SourceLoadResult LoadSource(std::string_view path,
                            const std::vector<std::uint8_t>& bytes);

}  // namespace cursive::core
