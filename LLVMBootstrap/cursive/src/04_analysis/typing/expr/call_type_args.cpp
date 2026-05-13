// =================================================================
// File: 04_analysis/typing/expr/call_type_args.cpp
// Construct: Call With Type Arguments Expression Type Checking
// Spec Section: 9.4, 13.1.2
// Spec Rules: T-Generic-Call, CallTypeArgs elaboration
// =================================================================
//
// NOTE: This module provides the type argument handling for generic procedure
// calls. The core functionality is shared with call.cpp through the
// BuildGenericCallSubst function.
//
// TYPING:
//   Gamma |- callee : forall <T_1, ..., T_n>. (P_1, ..., P_m) -> R
//   Type arguments: [A_1, ..., A_n]
//   All A_i satisfy bounds for T_i
//   Instantiate: (P_1', ..., P_m') -> R' = [A_i/T_i](signature)
//   ArgsOk(args, [P_1', ..., P_m'])
//   --------------------------------------------------
//   Gamma |- callee<A_1, ..., A_n>(args) : R'
//
// TYPE ARGUMENT SYNTAX:
//   - Generic params use semicolons: <T; U> in declarations
//   - Generic args use commas: <T, U> in calls
//
// =================================================================

#include "04_analysis/typing/expr/call_type_args.h"

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/memory/calls.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/typecheck.h"

namespace cursive::analysis::expr {

namespace {

static inline void SpecDefsCallTypeArgs() {
  SPEC_DEF("T-Generic-Call", "13.1.2");
  SPEC_DEF("CallTypeArgs-Elaboration", "9.4");
  SPEC_DEF("Generic-Call-ArgCount-Err", "14.2.4");
}

struct CallTypeArgsSubstResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeSubst subst;
};

static const ast::ASTModule* FindModuleForGenericCall(
    const ScopeContext& ctx,
    const ast::ModulePath& path) {
  for (const auto& mod : ctx.sigma.mods) {
    if (mod.path == path) {
      return &mod;
    }
  }
  return nullptr;
}

static const ast::ProcedureDecl* FindProcedureDecl(
    const ast::ASTModule& module, std::string_view name) {
  for (const auto& item : module.items) {
    if (const auto* proc = std::get_if<ast::ProcedureDecl>(&item)) {
      if (IdEq(proc->name, name)) {
        return proc;
      }
    }
  }
  return nullptr;
}

static std::size_t RequiredTypeArgCount(
    const std::vector<ast::TypeParam>& params) {
  std::size_t count = 0;
  for (const auto& param : params) {
    if (!param.default_type) {
      ++count;
    }
  }
  return count;
}

}  // namespace

static CallTypeArgsSubstResult BuildCallTypeArgsSubstChecked(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& type_args) {
  SpecDefsCallTypeArgs();
  CallTypeArgsSubstResult result;

  if (!callee) {
    return result;
  }

  std::string name;
  std::optional<ast::ModulePath> origin;

  // Resolve callee to find the generic procedure declaration
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&callee->node)) {
    const auto ent = ResolveValueName(ctx, ident->name);
    if (!ent || !ent->origin_opt) {
      return result;
    }
    origin = ent->origin_opt;
    name = ent->target_opt.value_or(std::string(ident->name));
  } else if (const auto* path_expr = std::get_if<ast::PathExpr>(&callee->node)) {
    origin = path_expr->path;
    name = path_expr->name;
  } else {
    return result;
  }

  const auto* module = FindModuleForGenericCall(ctx, *origin);
  if (!module) {
    return result;
  }

  const auto* proc = FindProcedureDecl(*module, name);
  if (!proc || !proc->generic_params) {
    return result;
  }

  const auto& params = proc->generic_params->params;
  const auto required = RequiredTypeArgCount(params);
  if (type_args.size() < required || type_args.size() > params.size()) {
    SPEC_RULE("Generic-Call-ArgCount-Err");
    result.diag_id = "E-TYP-2303";
    return result;
  }

  // Lower each type argument
  std::vector<TypeRef> lowered_args;
  lowered_args.reserve(type_args.size());
  for (const auto& arg : type_args) {
    const auto lowered = LowerType(ctx, arg);
    if (!lowered.ok) {
      result.diag_id = lowered.diag_id;
      return result;
    }
    lowered_args.push_back(lowered.type);
  }

  // Build substitution mapping generic parameters to type arguments
  result.ok = true;
  result.subst = BuildSubstitution(params, lowered_args);
  return result;
}

std::optional<TypeSubst> BuildCallTypeArgsSubst(
    const ScopeContext& ctx,
    const ast::ExprPtr& callee,
    const std::vector<std::shared_ptr<ast::Type>>& type_args) {
  const auto result = BuildCallTypeArgsSubstChecked(ctx, callee, type_args);
  if (!result.ok) {
    return std::nullopt;
  }
  return result.subst;
}

ExprTypeResult TypeCallTypeArgsExprImpl(const ScopeContext& ctx,
                                        const StmtTypeContext& type_ctx,
                                        const ast::CallTypeArgsExpr& expr,
                                        const TypeEnv& env) {
  SpecDefsCallTypeArgs();
  ExprTypeResult result;

  // Must have type arguments for this path
  if (expr.type_args.empty()) {
    result.diag_id = "CallTypeArgs-No-TypeArgs";
    return result;
  }

  // Build substitution from type arguments
  const auto subst_result =
      BuildCallTypeArgsSubstChecked(ctx, expr.callee, expr.type_args);
  if (!subst_result.ok) {
    result.diag_id = subst_result.diag_id.value_or("E-SEM-2533");
    return result;
  }

  auto type_expr = [&](const ast::ExprPtr& inner) {
    return TypeExpr(ctx, type_ctx, inner, env);
  };

  const auto arg_ctx_for = [&](const TypeRef& expected) {
    if (!expected) {
      return type_ctx;
    }
    const auto perm = PermOfType(expected);
    if (perm == Permission::Unique) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Write);
    }
    if (perm == Permission::Shared || perm == Permission::Const) {
      return WithSharedAccessMode(type_ctx, ast::KeyMode::Read);
    }
    return type_ctx;
  };
  PlaceTypeFn type_place = [&](const ast::ExprPtr& inner) {
    return TypePlace(ctx, type_ctx, inner, env);
  };
  ArgCheckFn check_expr = [&](const ast::ExprPtr& inner,
                              const TypeRef& expected) -> ArgCheckResult {
    const auto checked =
        CheckExprAgainst(ctx, arg_ctx_for(expected), inner, expected, env);
    return ArgCheckResult{checked.ok, checked.diag_id};
  };

  // Type the call with the type substitution applied
  const auto call = TypeCallWithSubst(ctx, expr.callee, expr.args,
                                       subst_result.subst, type_expr, &type_place,
                                       &check_expr);
  if (!call.ok) {
    result.diag_id = call.diag_id;
    result.diag_detail = call.diag_detail;
    return result;
  }

  // Check purity if required
  if (type_ctx.require_pure) {
    const auto callee_type = type_expr(expr.callee);
    if (callee_type.ok) {
      const auto* func =
          std::get_if<TypeFunc>(&StripPerm(callee_type.type)->node);
      if (func && !ParamsPure(ctx, func->params)) {
        result.diag_id = "E-SEM-2802";
        return result;
      }
    }
  }

  SPEC_RULE("T-Generic-Call");
  result.ok = true;
  result.type = call.type;
  return result;
}

}  // namespace cursive::analysis::expr
