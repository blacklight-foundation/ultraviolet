// =================================================================
// File: 03_analysis/types/expr/method_call.h
// Construct: Method Call Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-MethodCall, T-Record-MethodCall, T-Modal-Transition,
//             T-Modal-Method, T-Dynamic-MethodCall, T-Opaque-Project,
//             MethodCall-RecvPerm-Err, Step-MethodCall
// =================================================================
#pragma once

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Method Call Expression Typing
ExprTypeResult TypeMethodCallExprImpl(const ScopeContext& ctx,
                                      const StmtTypeContext& type_ctx,
                                      const ast::MethodCallExpr& expr,
                                      const TypeEnv& env,
                                      const core::Span& span);

}  // namespace ultraviolet::analysis::expr
