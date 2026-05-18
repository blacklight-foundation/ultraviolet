// =================================================================
// File: 04_analysis/typing/expr/closure_expr.cpp
// Construct: Closure Expression Type Checking
// Spec Section: SPECIFICATION.md Section 5.2 (Closures)
// =================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Lines 8789-8846
//
// Type checking rules:
//   - T-Closure-Local: Non-escaping closure with local captures
//   - T-Closure-Escaping: Escaping closure requires shared deps annotation
//   - T-ClosureCall: Closure invocation typing
//
// Grammar:
//   closure_expr       ::= "|" closure_param_list? "|" ("->" type)? closure_body
//   closure_param_list ::= closure_param ("," closure_param)*
//   closure_param      ::= "move"? identifier (":" type)?
//   closure_body       ::= expression | block_expr
//
// =================================================================

#include "04_analysis/typing/expr/closure_expr.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis::expr {

static bool SameSpan(const std::optional<core::Span>& lhs,
                     const core::Span& rhs) {
  if (!lhs.has_value()) {
    return false;
  }
  return lhs->file == rhs.file &&
         lhs->start_offset == rhs.start_offset &&
         lhs->end_offset == rhs.end_offset &&
         lhs->start_line == rhs.start_line &&
         lhs->start_col == rhs.start_col &&
         lhs->end_line == rhs.end_line &&
         lhs->end_col == rhs.end_col;
}

static bool HasDiagnosticAt(const core::DiagnosticStream& diags,
                            std::string_view code,
                            const core::Span& span) {
  for (const auto& diag : diags) {
    if (diag.code == code && SameSpan(diag.span, span)) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// TypeClosureExpr - Type check a closure expression
// =============================================================================
//
// SPEC_RULE("T-Closure-Local"):
//   Non-escaping closure: no shared deps required
//   Result type: TypeClosure([<move?, T_i> | i in 1..|params|], R, bottom)
//
// SPEC_RULE("T-Closure-Escaping"):
//   Escaping closure: shared deps annotation required
//   Result type: TypeClosure([<move?, T_i> | i in 1..|params|], R, <deps>)
//
// =============================================================================

ExprTypeResult TypeClosureExpr(const ast::ClosureExpr& expr,
                               const ScopeContext& ctx,
                               const StmtTypeContext& type_ctx,
                               const TypeEnv& env,
                               const ExprTypeFn& type_expr,
                               const IdentTypeFn& type_ident,
                               const PlaceTypeFn& type_place,
                               const TypeRef* expected_type) {
  SPEC_RULE("T-Closure-Local");
  SPEC_RULE("T-Closure-Escaping");

  ExprTypeResult result;
  const TypeClosure* expected_closure = nullptr;
  if (expected_type && *expected_type) {
    if (const auto stripped = StripPerm(*expected_type)) {
      expected_closure = std::get_if<TypeClosure>(&stripped->node);
    }
  }
  if (expected_closure && expr.params.size() != expected_closure->params.size()) {
    result.diag_id = "E-SEM-2532";
    return result;
  }

  // Determine parameter types
  std::vector<std::pair<bool, TypeRef>> param_types;
  std::vector<std::pair<std::string, TypeRef>> param_binds;
  param_binds.reserve(expr.params.size());
  for (std::size_t i = 0; i < expr.params.size(); ++i) {
    const auto& param = expr.params[i];
    TypeRef param_type = nullptr;
    if (param.type_opt) {
      // Type annotation provided - lower it
      auto lower_result = LowerType(ctx, param.type_opt);
      if (lower_result.type) {
        param_type = lower_result.type;
      }
    } else if (expected_closure) {
      param_type = expected_closure->params[i].second;
    }
    if (!param_type) {
      // Parameter inference requires an expected closure type context.
      result.diag_id = "Infer-Closure-Params-Err";
      return result;
    }
    param_types.push_back({param.move_capture, param_type});
    param_binds.push_back({param.name, param_type});
  }

  // Type-check closure body in a scope that includes closure parameters.
  TypeEnv body_env = PushScope(env);
  const auto intro =
      IntroAll(body_env, param_binds, ast::Mutability::Let, false);
  if (!intro.ok) {
    result.diag_id = intro.diag_id;
    return result;
  }
  body_env = intro.env;

  // Determine return type
  TypeRef ret_type = nullptr;
  if (expr.ret_type_opt) {
    auto lower_result = LowerType(ctx, expr.ret_type_opt);
    if (!lower_result.ok || !lower_result.type) {
      result.diag_id = lower_result.diag_id;
      return result;
    }
    ret_type = lower_result.type;
  }

  StmtTypeContext body_type_ctx = type_ctx;
  body_type_ctx.env_ref = &body_env;

  if (!ret_type && expected_closure) {
    ret_type = expected_closure->ret;
  }

  if (ret_type) {
    body_type_ctx.return_type = ret_type;
    const auto body_check =
        CheckExprAgainst(ctx, body_type_ctx, expr.body, ret_type, body_env);
    if (!body_check.ok) {
      result.diag_id = body_check.diag_id;
      return result;
    }
  } else {
    const auto body_type = TypeExpr(ctx, body_type_ctx, expr.body, body_env);
    if (!body_type.ok) {
      result.diag_id = body_type.diag_id;
      return result;
    }
    SPEC_RULE("Infer-Closure-Return");
    ret_type = body_type.type;
  }

  // Non-capturing closures are first-class function values.
  bool captures_any = false;
  bool captures_shared = false;
  {
    auto closure_expr = std::make_shared<ast::Expr>();
    closure_expr->node = expr;
    if (const auto capture_info =
            AnalyzeClosureCaptureInfo(closure_expr, env, nullptr)) {
      captures_any = capture_info->captures_any;
      captures_shared = capture_info->captures_shared;
    }
  }

  if (captures_shared && type_ctx.diags) {
    if (!HasDiagnosticAt(*type_ctx.diags, "W-CON-0009", expr.body->span)) {
      if (auto diag = core::MakeDiagnosticById("W-CON-0009", expr.body->span)) {
        core::Emit(*type_ctx.diags, *diag);
      }
    }
  }

  if (!captures_any) {
    SPEC_RULE("T-Closure-NonCapturing");
    if (expected_closure) {
      SPEC_RULE("T-Closure-NonCapturing-Expected");
      result.type =
          MakeTypeClosure(param_types, ret_type, expected_closure->deps_opt);
      result.ok = true;
      return result;
    }
    std::vector<TypeFuncParam> fn_params;
    fn_params.reserve(param_types.size());
    for (const auto& [is_move, param_type] : param_types) {
      TypeFuncParam fn_param;
      fn_param.mode = is_move ? std::optional<ParamMode>(ParamMode::Move)
                              : std::nullopt;
      fn_param.type = param_type;
      fn_params.push_back(std::move(fn_param));
    }
    result.type = MakeTypeFunc(std::move(fn_params), ret_type);
    result.ok = true;
    return result;
  }

  // Capturing closures carry an environment.
  result.type = MakeTypeClosure(param_types, ret_type, std::nullopt);
  result.ok = true;
  return result;
}

}  // namespace ultraviolet::analysis::expr
