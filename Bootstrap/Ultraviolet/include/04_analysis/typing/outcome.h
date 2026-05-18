#pragma once

#include <optional>
#include <string_view>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

struct OutcomeSig {
  TypeRef value;
  TypeRef error;
};

bool IsOutcomeTypePath(const ast::TypePath& path);

TypeRef MakeOutcomeType(TypeRef value_type, TypeRef error_type);

TypeRef MakeOutcomeStateType(TypeRef value_type,
                             TypeRef error_type,
                             std::string_view state);

std::optional<OutcomeSig> OutcomeSigOf(const TypeRef& type);

ast::ModalDecl BuildOutcomeModalDecl();

}  // namespace ultraviolet::analysis
