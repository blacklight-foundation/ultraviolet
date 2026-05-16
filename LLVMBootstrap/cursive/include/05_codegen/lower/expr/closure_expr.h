#pragma once

// =============================================================================
// File: 05_codegen/lower/expr/closure_expr.h
// SPEC REFERENCE: CursiveSpecification.md Lines 16260-16286 (Closure Lowering)
// =============================================================================
//
// Declares closure expression lowering per Section 6.4:
//   - Lower-Expr-Closure-NonCapturing: ClosureVal(null, sym)
//   - Lower-Expr-Closure-Capturing: Environment allocation + ClosureVal
//   - Lower-Closure-Call: IndirectCall with env_ptr
//
// =============================================================================

#include <string>
#include <unordered_set>
#include <vector>

#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// =============================================================================
// ClosureVal - Representation of a closure value (env_ptr, code_ptr)
// =============================================================================
//
// A ClosureVal consists of:
//   - env_ptr: Pointer to the captured environment (null for non-capturing)
//   - code_sym: Symbol name of the closure code procedure
//
// The closure code procedure has signature:
//   For non-capturing: (params...) -> ret
//   For capturing: (env_ptr, params...) -> ret
//
// =============================================================================

struct ClosureVal {
  IRValue env_ptr;
  std::string code_sym;
};

// Create a ClosureVal with environment pointer and code symbol
ClosureVal MakeClosureVal(const IRValue& env_ptr, const std::string& code_sym);

// Create a non-capturing ClosureVal (null environment pointer)
ClosureVal MakeNonCapturingClosureVal(const std::string& code_sym);

// =============================================================================
// LowerClosureExpr - Lower a closure expression
// =============================================================================
//
// SPEC_RULE("Lower-Expr-Closure-NonCapturing"):
//   If CaptureSet is empty, return ClosureVal(null, sym)
//
// SPEC_RULE("Lower-Expr-Closure-Capturing"):
//   - Get closure code symbol
//   - Compute closure environment layout (size, align, offsets)
//   - Lower capture environment: allocate env, store each capture
//   - Return ClosureVal(env_ptr, sym)
//
// Parameters:
//   params - The closure parameter list
//   ret_type_opt - Optional return type annotation
//   body - The closure body block
//   move_captures - Set of variable names that should be move-captured
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing the IR to set up the closure and the closure value
//
// =============================================================================

LowerResult LowerClosureExpr(
    const std::vector<ast::Param>& params,
    const ast::TypePtr& ret_type_opt,
    const ast::Block& body,
    const std::unordered_set<std::string>& move_captures,
    LowerCtx& ctx,
    const std::vector<analysis::TypeRef>* inferred_param_types = nullptr,
    const std::vector<std::optional<analysis::ParamMode>>* inferred_param_modes =
        nullptr,
    analysis::TypeRef inferred_ret_type = nullptr);

// Overload accepting the AST ClosureExpr node directly
LowerResult LowerClosureExpr(const ast::ClosureExpr& expr, LowerCtx& ctx);
LowerResult LowerClosureExpr(const ast::Expr& expr,
                             const ast::ClosureExpr& closure,
                             LowerCtx& ctx);

// =============================================================================
// LowerClosureCall - Lower a call to a closure value
// =============================================================================
//
// SPEC_RULE("Lower-Closure-Call"):
//   - Extract env_ptr and code_ptr from closure value
//   - Lower arguments
//   - Create IndirectCall(code_ptr, [env_ptr] ++ args)
//
// Parameters:
//   closure_expr - The expression evaluating to the closure value
//   args - The call arguments
//   ctx - The lowering context
//
// Returns:
//   LowerResult containing the call IR and result value
//
// =============================================================================

LowerResult LowerClosureCall(
    const ast::Expr& closure_expr,
    const std::vector<ast::Arg>& args,
    LowerCtx& ctx);

// =============================================================================
// Closure Type Utilities
// =============================================================================

// Check if a type is a closure type (tuple of (Ptr, FuncPtr))
bool IsClosureType(const analysis::TypeRef& type);

// Normalize a callable type alias before closure/function shape checks.
analysis::TypeRef NormalizeCallableAliasForLowering(const analysis::TypeRef& type,
                                                    const LowerCtx& ctx);

// Extract the function type from a closure type
analysis::TypeRef GetClosureFuncType(const analysis::TypeRef& closure_type);

}  // namespace cursive::codegen
