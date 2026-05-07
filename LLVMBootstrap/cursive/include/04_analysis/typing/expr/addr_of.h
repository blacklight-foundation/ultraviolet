// =================================================================
// File: 03_analysis/types/expr/addr_of.h
// Construct: Address-Of Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-AddrOf, AddrOf-NonPlace, AddrOf-Index-Array-NonUsize,
//             AddrOf-Index-Slice-NonUsize
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::expr {

// §5.2.12 Address-Of Expression Typing
ExprTypeResult TypeAddressOfExprImpl(const ScopeContext& ctx,
                                     const StmtTypeContext& type_ctx,
                                     const ast::AddressOfExpr& expr,
                                     const TypeEnv& env);

}  // namespace cursive::analysis::expr
