// =================================================================
// File: 03_analysis/types/type_lower.h
// Construct: Syntax Type to Analysis Type Lowering
// Spec Section: 5.2.3, 5.2.9
// =================================================================
#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

// Result of lowering a syntax type to an analysis type
struct LowerTypeResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

// §5.2.3 WF-Apply: Lower syntax type to analysis type
LowerTypeResult LowerType(const ScopeContext& ctx,
                          const std::shared_ptr<ast::Type>& type);

// Helper functions for lowering syntax constructs to analysis types

Permission LowerPermission(ast::TypePerm perm);

std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode);

RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual);

std::optional<StringState> LowerStringState(
    const std::optional<ast::StringState>& state);

std::optional<BytesState> LowerBytesState(
    const std::optional<ast::BytesState>& state);

std::optional<PtrState> LowerPtrState(
    const std::optional<ast::PtrState>& state);

}  // namespace ultraviolet::analysis
