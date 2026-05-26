// =================================================================
// File: 03_analysis/types/expr/deref.h
// Construct: Dereference Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Deref-Ptr, T-Deref-Raw, P-Deref-Ptr, P-Deref-Raw-Imm,
//             P-Deref-Raw-Mut, Deref-Null, Deref-Expired, Deref-Raw-Unsafe
// =================================================================
#pragma once

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/place_types.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Dereference Expression Typing (value context)
ExprTypeResult TypeDerefExprImpl(const ScopeContext& ctx,
                                 const StmtTypeContext& type_ctx,
                                 const ast::DerefExpr& expr,
                                 const TypeEnv& env,
                                 const core::Span& span);

// §5.2.12 Dereference Place Typing (place context)
PlaceTypeResult TypeDerefPlaceImpl(const ScopeContext& ctx,
                                   const StmtTypeContext& type_ctx,
                                   const ast::DerefExpr& expr,
                                   const TypeEnv& env,
                                   const core::Span& span);

}  // namespace ultraviolet::analysis::expr
