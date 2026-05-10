#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct StringBytesBuiltinMethodSig {
  Permission recv_perm = Permission::Const;
  TypeRef recv_type;
  std::vector<TypeFuncParam> params;
  TypeRef ret;
};

bool IsStringBytesBuiltinPath(const ast::ModulePath& path);
bool IsStringBuiltinName(std::string_view name);
bool IsBytesBuiltinName(std::string_view name);

std::optional<TypeRef> LookupStringBytesBuiltinType(
    const ast::ModulePath& path,
    std::string_view name);

std::optional<StringBytesBuiltinMethodSig> LookupStringBytesBuiltinMethodSig(
    const TypeRef& recv_base,
    std::string_view name);

}  // namespace cursive::analysis
