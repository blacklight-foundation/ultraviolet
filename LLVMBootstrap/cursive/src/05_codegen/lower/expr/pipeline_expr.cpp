// =============================================================================
// Pipeline Expression Lowering
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16288-16302 (Pipeline Lowering)
//   - Lower-Expr-Pipeline: e1 => e2 desugars to function/closure application
//   - IsFunc case: CallIR(v_2, [v_1])
//   - IsClosure case: IndirectCall(code, [env, v_1])
//
// The pipeline operator => passes the LHS value as the first argument to
// the RHS function or closure.
//
// SPEC RULE: (Lower-Expr-Pipeline)
//   Gamma |- LowerExpr(e_1) => <IR_1, v_1>
//   Gamma |- LowerExpr(e_2) => <IR_2, v_2>
//   type(e_2) = TypeFunc(_, _) or TypeClosure(_, _, _)
//   -------------------------------------------------------------------
//   Gamma |- LowerExpr(Pipeline(e_1, e_2)) => <SeqIR(IR_1, IR_2, IR_call), v_call>
//
// Where:
//   IsFunc(TypeFunc(_, _)) => IR_call = CallIR(v_2, [v_1])
//   IsClosure(TypeClosure(_, _, _)) => IR_call = IndirectCall(code, [env, v_1])
//     where (env, code) = extract closure components from v_2
//
// =============================================================================

#include "05_codegen/lower/expr/pipeline_expr.h"

#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/checks/panic.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/expr/closure_expr.h"

namespace cursive::codegen {

namespace {

/// Helper predicate: IsFunc(TypeFunc(_, _)) = true, otherwise false
bool IsFunc(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  const auto stripped = analysis::StripPerm(type);
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<analysis::TypeFunc>(stripped->node);
}

analysis::TypeRef PipelineResultType(const analysis::TypeRef& type) {
  if (!type) {
    return nullptr;
  }

  analysis::TypeRef stripped = analysis::StripPerm(type);
  if (!stripped) {
    stripped = type;
  }
  if (!stripped) {
    return nullptr;
  }

  if (const auto* fn = std::get_if<analysis::TypeFunc>(&stripped->node)) {
    return fn->ret;
  }
  if (const auto* closure = std::get_if<analysis::TypeClosure>(&stripped->node)) {
    return closure->ret;
  }

  return nullptr;
}

}  // namespace

// =============================================================================
// LowerPipelineExpr - Lower pipeline expression to IR
// =============================================================================
//
// (Lower-Expr-Pipeline)
//   Gamma |- LowerExpr(e_1) => <IR_1, v_1>
//   Gamma |- LowerExpr(e_2) => <IR_2, v_2>
//   type(e_2) = TypeFunc(_, _)
//   ---------------------------------------------------------------
//   Gamma |- LowerExpr(e_1 => e_2) => <SeqIR(IR_1, IR_2, CallIR(v_2, [v_1])), v_call>
//
// Pipeline expressions desugar to function application where the LHS
// becomes the first argument to the RHS function.

LowerResult LowerPipelineExpr(const ast::BinaryExpr& expr, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Pipeline");

  // Verify this is actually a pipeline operator.
  if (expr.op != "=>") {
    ctx.ReportCodegenFailure();
    IRValue bad = ctx.FreshTempValue("pipeline_err");
    return LowerResult{EmptyIR(), bad};
  }

  // Lower the LHS expression (the value to pass)
  auto lhs_result = LowerExpr(*expr.lhs, ctx);
  IRPtr lhs_ir = lhs_result.ir;
  IRValue v_1 = lhs_result.value;

  // Lower the RHS expression (the function to call)
  auto rhs_result = LowerExpr(*expr.rhs, ctx);
  IRPtr rhs_ir = rhs_result.ir;
  IRValue v_2 = rhs_result.value;

  // Get the type of the RHS to determine call strategy
  analysis::TypeRef rhs_type;
  if (ctx.expr_type) {
    rhs_type = ctx.expr_type(*expr.rhs);
  }

  // Create the result value
  IRValue result_value = ctx.FreshTempValue("pipeline");

  // Determine call strategy based on RHS type
  if (IsClosureType(rhs_type)) {
    // Closure case: extract env and code, create indirect call
    // IR_call = IndirectCall(code, [env, v_1])
    //
    // The closure value is a pair (env_ptr, code_ptr). We extract these and
    // create an indirect call through the code pointer with the environment
    // as the first argument.
    SPEC_RULE("Lower-Pipeline-Closure");

    // Extract env_ptr and code_ptr from closure tuple value.
    // Closure lowering materializes closures as tuple literals:
    //   (env_ptr, code_ptr)
    IRValue env_ptr = ctx.FreshTempValue("closure_env");
    DerivedValueInfo env_info;
    env_info.kind = DerivedValueInfo::Kind::Tuple;
    env_info.base = v_2;
    env_info.tuple_index = 0;
    ctx.RegisterDerivedValue(env_ptr, env_info);

    IRValue code_ptr = ctx.FreshTempValue("closure_code");
    DerivedValueInfo code_info;
    code_info.kind = DerivedValueInfo::Kind::Tuple;
    code_info.base = v_2;
    code_info.tuple_index = 1;
    ctx.RegisterDerivedValue(code_ptr, code_info);

    if (rhs_type) {
      analysis::TypeRef stripped = analysis::StripPerm(rhs_type);
      if (!stripped) {
        stripped = rhs_type;
      }
      if (const auto* closure = stripped
                                    ? std::get_if<analysis::TypeClosure>(&stripped->node)
                                    : nullptr) {
        std::vector<analysis::TypeFuncParam> fn_params;
        analysis::TypeFuncParam env_param;
        env_param.mode = analysis::ParamMode::Move;
        env_param.type =
            analysis::MakeTypePtr(analysis::MakeTypePrim("u8"),
                                  analysis::PtrState::Valid);
        fn_params.push_back(std::move(env_param));
        for (const auto& [is_move, param_type] : closure->params) {
          analysis::TypeFuncParam p;
          if (is_move) {
            p.mode = analysis::ParamMode::Move;
          }
          p.type = param_type;
          fn_params.push_back(std::move(p));
        }
        analysis::TypeFuncParam panic_param;
        panic_param.mode = analysis::ParamMode::Move;
        panic_param.type = PanicOutType();
        fn_params.push_back(std::move(panic_param));
        ctx.RegisterValueType(
            code_ptr,
            analysis::MakeTypeFunc(std::move(fn_params), closure->ret));
      }
    }
    if (analysis::TypeRef ret_type = PipelineResultType(rhs_type)) {
      ctx.RegisterValueType(result_value, ret_type);
    }

    // Create indirect call through code pointer with env as first arg
    IRCall call;
    call.callee = code_ptr;
    call.args = {env_ptr, v_1};
    IRValue panic_out;
    panic_out.kind = IRValue::Kind::Local;
    panic_out.name = std::string(kPanicOutName);
    call.args.push_back(panic_out);
    call.result = result_value;
    return LowerResult{SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call)), PanicCheck(ctx)}),
                       result_value};
  }

  // Function case: create direct call
  // IR_call = CallIR(v_2, [v_1])
  if (IsFunc(rhs_type) || !rhs_type) {
    SPEC_RULE("Lower-Pipeline-Func");

    IRCall call;
    call.callee = v_2;
    call.args = {v_1};
    call.result = result_value;

    // Determine if we need panic out parameter
    bool needs_panic_out = true;
    if (v_2.kind == IRValue::Kind::Symbol) {
      needs_panic_out = ctx.NeedsPanicOutForSymbol(v_2.name);
    }

    if (needs_panic_out) {
      IRValue panic_out;
      panic_out.kind = IRValue::Kind::Local;
      panic_out.name = std::string(kPanicOutName);
      call.args.push_back(panic_out);
    }

    if (needs_panic_out) {
      return LowerResult{
          SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call)), PanicCheck(ctx)}),
          result_value};
    }
    return LowerResult{SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call))}),
                       result_value};
  }

  // Fallback: RHS type is neither function nor closure
  // This should not happen for well-typed pipeline expressions
  ctx.ReportCodegenFailure();
  return LowerResult{SeqIR({lhs_ir, rhs_ir}), result_value};
}

// =============================================================================
// LowerPipelineExpr - Overload accepting ast::PipelineExpr directly
// =============================================================================

LowerResult LowerPipelineExpr(const ast::PipelineExpr& expr, LowerCtx& ctx) {
  SPEC_RULE("Lower-Expr-Pipeline");

  if (!expr.lhs || !expr.rhs) {
    ctx.ReportCodegenFailure();
    return LowerResult{EmptyIR(), ctx.FreshTempValue("pipeline_err")};
  }

  // Lower the LHS expression (the value to pass)
  auto lhs_result = LowerExpr(*expr.lhs, ctx);
  IRPtr lhs_ir = lhs_result.ir;
  IRValue v_1 = lhs_result.value;

  // Lower the RHS expression (the function to call)
  auto rhs_result = LowerExpr(*expr.rhs, ctx);
  IRPtr rhs_ir = rhs_result.ir;
  IRValue v_2 = rhs_result.value;

  // Get the type of the RHS to determine call strategy
  analysis::TypeRef rhs_type;
  if (ctx.expr_type) {
    rhs_type = ctx.expr_type(*expr.rhs);
  }

  // Create the result value
  IRValue result_value = ctx.FreshTempValue("pipeline");

  // Determine call strategy based on RHS type
  if (IsClosureType(rhs_type)) {
    // Closure case: extract env and code, create indirect call
    SPEC_RULE("Lower-Pipeline-Closure");

    IRValue env_ptr = ctx.FreshTempValue("closure_env");
    DerivedValueInfo env_info;
    env_info.kind = DerivedValueInfo::Kind::Tuple;
    env_info.base = v_2;
    env_info.tuple_index = 0;
    ctx.RegisterDerivedValue(env_ptr, env_info);

    IRValue code_ptr = ctx.FreshTempValue("closure_code");
    DerivedValueInfo code_info;
    code_info.kind = DerivedValueInfo::Kind::Tuple;
    code_info.base = v_2;
    code_info.tuple_index = 1;
    ctx.RegisterDerivedValue(code_ptr, code_info);

    if (rhs_type) {
      analysis::TypeRef stripped = analysis::StripPerm(rhs_type);
      if (!stripped) {
        stripped = rhs_type;
      }
      if (const auto* closure = stripped
                                    ? std::get_if<analysis::TypeClosure>(&stripped->node)
                                    : nullptr) {
        std::vector<analysis::TypeFuncParam> fn_params;
        analysis::TypeFuncParam env_param;
        env_param.mode = analysis::ParamMode::Move;
        env_param.type =
            analysis::MakeTypePtr(analysis::MakeTypePrim("u8"),
                                  analysis::PtrState::Valid);
        fn_params.push_back(std::move(env_param));
        for (const auto& [is_move, param_type] : closure->params) {
          analysis::TypeFuncParam p;
          if (is_move) {
            p.mode = analysis::ParamMode::Move;
          }
          p.type = param_type;
          fn_params.push_back(std::move(p));
        }
        analysis::TypeFuncParam panic_param;
        panic_param.mode = analysis::ParamMode::Move;
        panic_param.type = PanicOutType();
        fn_params.push_back(std::move(panic_param));
        ctx.RegisterValueType(
            code_ptr,
            analysis::MakeTypeFunc(std::move(fn_params), closure->ret));
      }
    }
    if (analysis::TypeRef ret_type = PipelineResultType(rhs_type)) {
      ctx.RegisterValueType(result_value, ret_type);
    }

    IRCall call;
    call.callee = code_ptr;
    call.args = {env_ptr, v_1};
    IRValue panic_out;
    panic_out.kind = IRValue::Kind::Local;
    panic_out.name = std::string(kPanicOutName);
    call.args.push_back(panic_out);
    call.result = result_value;
    return LowerResult{SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call)), PanicCheck(ctx)}),
                       result_value};
  }

  // Function case: create direct call
  if (IsFunc(rhs_type) || !rhs_type) {
    SPEC_RULE("Lower-Pipeline-Func");

    IRCall call;
    call.callee = v_2;
    call.args = {v_1};
    call.result = result_value;

    bool needs_panic_out = true;
    if (v_2.kind == IRValue::Kind::Symbol) {
      needs_panic_out = ctx.NeedsPanicOutForSymbol(v_2.name);
    }

    if (needs_panic_out) {
      IRValue panic_out;
      panic_out.kind = IRValue::Kind::Local;
      panic_out.name = std::string(kPanicOutName);
      call.args.push_back(panic_out);
    }

    if (needs_panic_out) {
      return LowerResult{
          SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call)), PanicCheck(ctx)}),
          result_value};
    }
    return LowerResult{SeqIR({lhs_ir, rhs_ir, MakeIR(std::move(call))}),
                       result_value};
  }

  ctx.ReportCodegenFailure();
  return LowerResult{SeqIR({lhs_ir, rhs_ir}), result_value};
}

}  // namespace cursive::codegen
