// =================================================================
// File: 04_analysis/typing/expr/race_expr.cpp
// Construct: Race Expression Type Checking
// Spec Section: 17.3.6
// Spec Rules: T-Race, T-Race-Stream
// =================================================================
#include "00_core/assert_spec.h"
#include "04_analysis/composite/unions.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsRace() {
  SPEC_DEF("T-Race", "17.3.6");
  SPEC_DEF("T-Race-Stream", "17.3.6");
}

// Helper to check type equality across multiple types
bool AllEqTypes(const std::vector<TypeRef>& types,
                std::optional<std::string_view>& diag_id) {
  if (types.empty()) {
    return true;
  }
  const auto& base = types.front();
  for (std::size_t i = 1; i < types.size(); ++i) {
    const auto eq = TypeEquiv(base, types[i]);
    if (!eq.ok) {
      diag_id = eq.diag_id;
      return false;
    }
    if (!eq.equiv) {
      return false;
    }
  }
  return true;
}

// Helper to introduce pattern bindings into environment
struct IntroResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeEnv env;
};

IntroResult IntroAll(const TypeEnv& env,
                     const std::vector<std::pair<std::string, TypeRef>>& binds) {
  if (binds.empty()) {
    return {true, std::nullopt, env};
  }
  TypeEnv current = env;
  for (const auto& [name, type] : binds) {
    TypeBinding binding{ast::Mutability::Let, type};
    const auto key = IdKeyOf(name);
    if (current.scopes.empty()) {
      return {false, std::nullopt, env};
    }
    current.scopes.back().emplace(key, binding);
  }
  return {true, std::nullopt, std::move(current)};
}

TypeRef MakeTypePathWithArgs(TypePath path, std::vector<TypeRef> args) {
  TypePathType node;
  node.path = std::move(path);
  node.generic_args = std::move(args);
  return MakeType(node);
}

}  // namespace

// §17.3.6 Race Expression Typing
//
// race { arm1, arm2, ... }
//
// Typing rule (T-Race - return handlers):
// All arms have return handlers (-> |v| expr)
// Gamma |- async_i : Async<T_i, (), R_i, E_i>
// All handlers return type T
// --------------------------------------------------
// Gamma |- race {...} : T | E_union
//
// Typing rule (T-Race-Stream - yield handlers):
// All arms have yield handlers (-> |v| yield expr)
// Inside async context
// --------------------------------------------------
// Gamma |- race {...} : Stream<T, E_union>
//
ExprTypeResult TypeRaceExpr(const ScopeContext& ctx,
                            const StmtTypeContext& type_ctx,
                            const ast::RaceExpr& expr,
                            const TypeEnv& env,
                            const ExprTypeFn& type_expr,
                            const IdentTypeFn& type_ident,
                            const PlaceTypeFn& type_place) {
  SPEC_RULE("T-Race");
  ExprTypeResult result;

  // Race requires at least 2 arms
  if (expr.arms.size() < 2) {
    result.diag_id = "E-CON-0260";
    return result;
  }

  // Check handler kind consistency - all must be same type
  bool any_return = false;
  bool any_yield = false;
  for (const auto& arm : expr.arms) {
    if (arm.handler.kind == ast::RaceHandlerKind::Yield) {
      any_yield = true;
    } else {
      any_return = true;
    }
  }
  if (any_return && any_yield) {
    result.diag_id = "E-CON-0263";  // Mixed handler types
    return result;
  }

  const bool yield_mode = any_yield;
  std::vector<TypeRef> handler_types;
  std::vector<TypeRef> error_types;
  handler_types.reserve(expr.arms.size());
  error_types.reserve(expr.arms.size());

  for (const auto& arm : expr.arms) {
    // Type the async expression
    const auto expr_result = type_expr(arm.expr);
    if (!expr_result.ok) {
      result.diag_id = expr_result.diag_id;
      return result;
    }

    // Extract async signature
    const auto async_sig = AsyncSigOf(ctx, expr_result.type);
    if (!async_sig.has_value()) {
      result.diag_id = "E-CON-0261";  // Not an async type
      return result;
    }

    // Validate async signature based on mode
    if (!yield_mode) {
      // Return mode: async must have Out = ()
      if (!IsPrimType(async_sig->out, "()")) {
        result.diag_id = "E-CON-0262";
        return result;
      }
      if (!IsPrimType(async_sig->in, "()")) {
        result.diag_id = "E-CON-0261";
        return result;
      }
    } else {
      // Yield mode: async must have In = ()
      if (!IsPrimType(async_sig->in, "()")) {
        result.diag_id = "E-CON-0261";
        return result;
      }
    }

    // Determine the pattern type based on mode
    const TypeRef pat_type = yield_mode ? async_sig->out : async_sig->result;

    // Type the pattern
    const auto pat = TypePattern(ctx, arm.pattern, pat_type);
    if (!pat.ok) {
      result.diag_id = pat.diag_id;
      return result;
    }

    // Introduce pattern bindings into scope for handler
    TypeEnv inner = PushScope(env);
    const auto intro = IntroAll(inner, pat.bindings);
    if (!intro.ok) {
      result.diag_id = intro.diag_id;
      return result;
    }

    // Type the handler expression
    const auto handler_result = TypeExpr(ctx, type_ctx, arm.handler.value, intro.env);
    if (!handler_result.ok) {
      result.diag_id = handler_result.diag_id;
      return result;
    }

    handler_types.push_back(handler_result.type);
    error_types.push_back(async_sig->err);
  }

  // All handler types must match
  std::optional<std::string_view> eq_diag;
  if (!AllEqTypes(handler_types, eq_diag)) {
    if (eq_diag.has_value()) {
      result.diag_id = eq_diag;
      return result;
    }
    result.diag_id = "E-CON-0261";
    return result;
  }

  if (!yield_mode) {
    // T-Race: Return mode
    // Result type is handler type | all error types
    std::vector<TypeRef> members;
    members.reserve(1 + error_types.size());
    members.push_back(handler_types.front());
    members.insert(members.end(), error_types.begin(), error_types.end());
    const auto union_type = MakeTypeUnion(std::move(members));
    result.ok = true;
    if (union_type && std::holds_alternative<TypeUnion>(union_type->node)) {
      const auto intro = TypeUnionIntro(ctx, handler_types.front(), union_type);
      if (!intro.ok) {
        result.diag_id = intro.diag_id;
        return result;
      }
      result.type = intro.type;
    } else {
      result.type = union_type;
    }
    return result;
  }

  // T-Race-Stream: Yield mode
  // Result type is Stream<T, E_union>
  SPEC_RULE("T-Race-Stream");
  std::vector<TypeRef> err_members;
  err_members.reserve(error_types.size());
  for (const auto& err : error_types) {
    err_members.push_back(err);
  }
  const TypeRef err_union_raw = MakeTypeUnion(std::move(err_members));
  result.ok = true;
  TypeRef err_union = err_union_raw;
  if (err_union_raw && std::holds_alternative<TypeUnion>(err_union_raw->node)) {
    const auto err_intro = TypeUnionIntro(ctx, error_types.front(), err_union_raw);
    if (!err_intro.ok) {
      result.diag_id = err_intro.diag_id;
      return result;
    }
    err_union = err_intro.type;
  }
  result.type = MakeTypePathWithArgs({"Stream"}, {handler_types.front(), err_union});
  return result;
}

}  // namespace ultraviolet::analysis::expr
