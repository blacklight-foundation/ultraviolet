#pragma once

#include <optional>
#include <string_view>

#include "04_analysis/memory/calls.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

ExprTypeResult TypeTupleExpr(const ScopeContext& ctx,
                             const ast::TupleExpr& expr,
                             const ExprTypeFn& type_expr);

ExprTypeResult TypeTupleAccessValue(const ScopeContext& ctx,
                                    const ast::TupleAccessExpr& expr,
                                    const ExprTypeFn& type_expr);

PlaceTypeResult TypeTupleAccessPlace(const ScopeContext& ctx,
                                     const ast::TupleAccessExpr& expr,
                                     const PlaceTypeFn& type_place);

}  // namespace ultraviolet::analysis
