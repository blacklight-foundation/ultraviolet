// =============================================================================
// File: 04_analysis/typing/expr/path.cpp
// Path Expression Typing
// Spec Section: 5.2
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   Section 5.2: Static Semantics - Path Resolution
//   - Qualified name resolution
//   - Module path lookup
//   - Type path vs value path
//
// =============================================================================

#include "04_analysis/typing/expr/path.h"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/function_types.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_infer.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::expr {

namespace {

static inline void SpecDefsPath() {
  SPEC_DEF("T-Ident", "5.2");
  SPEC_DEF("P-Ident", "5.2");
  SPEC_DEF("T-Path-Value", "5.2");
  SPEC_DEF("T-Path-Type", "5.2");
  SPEC_DEF("PathResolution", "5.2");
  SPEC_DEF("VisibilityCheck", "5.2");
  SPEC_DEF("QualifiedName", "5.2");
  SPEC_DEF("ModulePathLookup", "5.2");
}

// Convert module path to type path
static TypePath ToTypePath(const ast::ModulePath& module_path) {
  TypePath result;
  result.reserve(module_path.size());
  for (const auto& segment : module_path) {
    result.push_back(segment);  // segment is already a string (Identifier)
  }
  return result;
}

// Look up entity in scope chain
static std::optional<Entity> LookupInScopes(const ScopeContext& ctx,
                                             std::string_view name) {
  // Search from innermost to outermost scope
  for (const auto& scope : ctx.scopes) {
    auto it = scope.find(std::string(name));
    if (it != scope.end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

// Look up path in type declarations
static std::optional<TypeDecl> LookupTypePath(const ScopeContext& ctx,
                                               const TypePath& path) {
  auto it = ctx.sigma.types.find(path);
  if (it != ctx.sigma.types.end()) {
    return it->second;
  }
  return std::nullopt;
}

// Look up class declaration
static std::optional<ast::ClassDecl> LookupClassPath(const ScopeContext& ctx,
                                                      const TypePath& path) {
  auto it = ctx.sigma.classes.find(path);
  if (it != ctx.sigma.classes.end()) {
    return it->second;
  }
  return std::nullopt;
}

static bool IsComptimeTypingEnv(const TypeEnv& env) {
  return BindOf(env, "diagnostics").has_value() ||
         BindOf(env, "introspect").has_value() ||
         BindOf(env, "emitter").has_value() ||
         BindOf(env, "files").has_value() ||
         BindOf(env, "target").has_value();
}

static const ast::ASTModule* FindModuleByPath(const ScopeContext& ctx,
                                              const ast::ModulePath& path) {
  for (const auto& mod : ctx.sigma.mods) {
    if (mod.path == path) {
      return &mod;
    }
  }
  return nullptr;
}

static bool ModuleHasComptimeProcedure(const ast::ASTModule& module,
                                       std::string_view name) {
  for (const auto& proc : module.comptime_procedures) {
    if (IdEq(proc.name, name)) {
      return true;
    }
  }
  for (const auto& item : module.items) {
    if (const auto* proc = std::get_if<ast::ComptimeProcedureDecl>(&item);
        proc != nullptr && IdEq(proc->name, name)) {
      return true;
    }
  }
  return false;
}

static bool IsRuntimeComptimeProcedureRef(const ScopeContext& ctx,
                                          const ast::ModulePath& origin,
                                          std::string_view name,
                                          const TypeEnv& env) {
  if (IsComptimeTypingEnv(env)) {
    return false;
  }
  const auto* module = FindModuleByPath(ctx, origin);
  return module != nullptr && ModuleHasComptimeProcedure(*module, name);
}

}  // namespace

// (T-Path-Value) Path Expression Typing
ExprTypeResult TypePathExprImpl(const ScopeContext& ctx,
                                const ast::PathExpr& expr,
                                const TypeEnv& env) {
  SpecDefsPath();
  ExprTypeResult result;

  const ast::ModulePath origin = expr.path.empty() ? ctx.current_module : expr.path;
  if (IsRuntimeComptimeProcedureRef(ctx, origin, expr.name, env)) {
    result.diag_id = "E-CTE-0034";
    return result;
  }

  const auto value_type = ValuePathType(ctx, expr.path, expr.name);
  if (!value_type.ok) {
    result.diag_id = value_type.diag_id;
    return result;
  }
  if (value_type.type) {
    SPEC_RULE("T-Path-Value");
    result.ok = true;
    result.type = value_type.type;
    return result;
  }
  result.diag_id = "ResolveExpr-Ident-Err";
  result.diag_detail = "identifier '" + std::string(expr.name) + "'";
  return result;
}

// (T-Ident) Identifier Expression Value Typing
ExprTypeResult TypeIdentifierExprImpl(const ScopeContext& ctx,
                                      const ast::IdentifierExpr& expr,
                                      const TypeEnv& env) {
  SpecDefsPath();
  ExprTypeResult result;

  const std::string& name = expr.name;

  if (const auto binding = BindOf(env, name); binding.has_value()) {
    SPEC_RULE("T-Ident");
    result.ok = true;
    result.type = binding->type;
    return result;
  }

  if (const auto ent = ResolveValueName(ctx, name);
      ent && ent->origin_opt.has_value()) {
    const std::string resolved_name = ent->target_opt.value_or(std::string(name));
    if (IsRuntimeComptimeProcedureRef(ctx, *ent->origin_opt, resolved_name, env)) {
      result.diag_id = "E-CTE-0034";
      return result;
    }
  } else if (IsRuntimeComptimeProcedureRef(ctx, ctx.current_module, name, env)) {
    result.diag_id = "E-CTE-0034";
    return result;
  }

  const auto value_type = ValuePathType(ctx, ctx.current_module, name);
  if (!value_type.ok) {
    result.diag_id = value_type.diag_id;
    return result;
  }
  if (value_type.type) {
    SPEC_RULE("T-Ident");
    result.ok = true;
    result.type = value_type.type;
    return result;
  }

  SPEC_RULE("ResolveExpr-Ident-Err");
  result.diag_id = "ResolveExpr-Ident-Err";
  result.diag_detail = "identifier '" + name + "'";
  return result;
}

// (P-Ident) Identifier Expression Place Typing
PlaceTypeResult TypeIdentifierPlaceImpl(const ScopeContext& ctx,
                                        const ast::IdentifierExpr& expr,
                                        const TypeEnv& env) {
  SpecDefsPath();
  PlaceTypeResult result;

  const std::string& name = expr.name;

  if (const auto binding = BindOf(env, name); binding.has_value()) {
    SPEC_RULE("P-Ident");
    result.ok = true;
    result.type = binding->type;
    return result;
  }

  if (const auto ent = ResolveValueName(ctx, name);
      ent.has_value() && ent->origin_opt.has_value()) {
    const auto resolved_name = ent->target_opt.value_or(std::string(name));
    const auto resolved_static =
        LookupModuleStatic(ctx, *ent->origin_opt, resolved_name);
    if (!resolved_static.ok) {
      result.diag_id = resolved_static.diag_id;
      return result;
    }
    if (resolved_static.type) {
      SPEC_RULE("P-Ident");
      result.ok = true;
      result.type = resolved_static.type;
      return result;
    }
  }

  const auto static_lookup = LookupModuleStatic(ctx, ctx.current_module, name);
  if (!static_lookup.ok) {
    result.diag_id = static_lookup.diag_id;
    return result;
  }
  if (static_lookup.type) {
    SPEC_RULE("P-Ident");
    result.ok = true;
    result.type = static_lookup.type;
    return result;
  }

  SPEC_RULE("ResolveExpr-Ident-Err");
  result.diag_id = "ResolveExpr-Ident-Err";
  result.diag_detail = "identifier '" + name + "'";
  return result;
}

}  // namespace ultraviolet::analysis::expr
