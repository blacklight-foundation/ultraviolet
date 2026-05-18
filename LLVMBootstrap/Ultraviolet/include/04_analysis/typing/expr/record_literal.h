// =================================================================
// File: 03_analysis/types/expr/record_literal.h
// Construct: Record Literal Expression Type Checking
// Spec Section: 5.2.12
// Spec Rules: T-Record-Literal, T-Modal-State-Intro, Record-FieldInit-Dup,
//             Record-Field-Unknown, Record-Field-NotVisible,
//             Record-FieldInit-Missing, Record-Field-NonBitcopy-Move
// =================================================================
#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

// §5.2.12 Record Literal Expression Typing
ExprTypeResult TypeRecordExprImpl(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::RecordExpr& expr,
                                  const TypeEnv& env,
                                  const TypeRef* expected_type = nullptr);

}  // namespace ultraviolet::analysis::expr
