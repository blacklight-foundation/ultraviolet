#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// ============================================================================
// §6.5 Statement Lowering Judgments
// ============================================================================

// §6.5 LowerStmt - main statement lowering entry point
IRPtr LowerStmt(const ast::Stmt& stmt, LowerCtx& ctx);

// §6.5 LowerStmtList - lower a list of statements
IRPtr LowerStmtList(const std::vector<ast::Stmt>& stmts, LowerCtx& ctx);

// §6.5 LowerBlock - lower a block (statements + optional tail expression)
LowerResult LowerBlock(const ast::Block& block, LowerCtx& ctx);

// ============================================================================
// §6.5 Specific Statement Forms
// ============================================================================

// §6.5 Lower-Stmt-Let-* (let bindings)
IRPtr LowerLetStmt(const ast::LetStmt& stmt, LowerCtx& ctx);

// §6.5 Lower-Stmt-Var-* (var bindings)
IRPtr LowerVarStmt(const ast::VarStmt& stmt, LowerCtx& ctx);

// §6.5 Lower-Stmt-UsingLocal (compile-time alias; lowers to a no-op)
IRPtr LowerUsingLocalStmt(const ast::UsingLocalStmt& stmt, LowerCtx& ctx);

// §6.5 Lower-Stmt-Assignment
IRPtr LowerAssignStmt(const ast::AssignStmt& stmt, LowerCtx& ctx);

// §6.5 Lower-Stmt-OpAssignment
IRPtr LowerCompoundAssignStmt(const ast::CompoundAssignStmt& stmt,
                              LowerCtx& ctx);

// §6.5 Lower-Stmt-Expr
IRPtr LowerExprStmt(const ast::ExprStmt& stmt, LowerCtx& ctx);

// §6.5 Lower-Stmt-Return
IRPtr LowerReturnStmt(const ast::ReturnStmt& stmt,
                      LowerCtx& ctx,
                      const std::vector<TempValue>& temps);

// Emit dynamic postcondition check for a concrete return value when
// ctx.active_contract_postcondition is set.
IRPtr EmitDynamicPostconditionCheckForReturn(const IRValue& return_value,
                                             LowerCtx& ctx);

// §6.5 Lower-Stmt-Break
IRPtr LowerBreakStmt(const ast::BreakStmt& stmt,
                     LowerCtx& ctx,
                     const std::vector<TempValue>& temps);

// §6.5 Lower-Stmt-Continue
IRPtr LowerContinueStmt(const ast::ContinueStmt& stmt,
                        LowerCtx& ctx,
                        const std::vector<TempValue>& temps);

// ============================================================================
// §6.5 Loop Lowering
// ============================================================================

// §6.5 Lower-Loop-Infinite
LowerResult LowerLoopInfinite(const ast::Expr& expr,
                              const ast::LoopInfiniteExpr& loop_expr,
                              LowerCtx& ctx);

// §6.5 Lower-Loop-Conditional
LowerResult LowerLoopConditional(const ast::Expr& expr,
                                 const ast::LoopConditionalExpr& loop_expr,
                                 LowerCtx& ctx);

// §6.5 Lower-Loop-Iter
LowerResult LowerLoopIter(const ast::Expr& expr,
                          const ast::LoopIterExpr& loop_expr,
                          LowerCtx& ctx);

// §6.5 LowerLoop - dispatch to appropriate loop lowering
LowerResult LowerLoop(const ast::Expr& loop, LowerCtx& ctx);

// ============================================================================
// §6.5 Cleanup at Statement Boundaries
// ============================================================================

// §6.5 CleanupList - temporaries to drop at end of statement
IRPtr CleanupList(const std::vector<TempValue>& temps, LowerCtx& ctx);

// Emit SPEC_RULE anchors for all §6.5 rules
void AnchorStmtLoweringRules();

}  // namespace cursive::codegen
