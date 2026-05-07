#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct EnumDiscResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::vector<std::uint64_t> discs;
  std::uint64_t max_disc = 0;
};

EnumDiscResult EnumDiscriminants(const ast::EnumDecl& decl);

}  // namespace cursive::analysis
