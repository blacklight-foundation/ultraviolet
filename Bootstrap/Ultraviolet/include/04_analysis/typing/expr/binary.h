// =================================================================
// File: 03_analysis/types/expr/binary.h
// Construct: Binary Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Arith, T-Bitwise, T-Shift, T-Compare-Eq, T-Compare-Ord, T-Logical
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Binary Expression Typing
// Handles: +, -, *, /, %, **, &, |, ^, <<, >>, ==, !=, <, <=, >, >=, &&, ||
ExprTypeResult TypeBinaryExprImpl(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::BinaryExpr& expr,
                                  const TypeEnv& env);

}  // namespace ultraviolet::analysis::expr
