#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

struct NetworkMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

bool IsNetworkClassPath(const ast::ClassPath& path);

std::optional<NetworkMethodSig> LookupNetworkMethodSig(
    std::string_view name);

}  // namespace cursive::analysis
