#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostics.h"
#include "01_project/project.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

using IdKey = std::string;
using PathKey = std::vector<IdKey>;
using ExprTypeMap = std::unordered_map<const ast::Expr*, TypeRef>;
using DynamicRefineExprMap =
    std::unordered_map<const ast::Expr*, std::vector<TypeRef>>;
using GenericCallSubstMap = std::unordered_map<const ast::CallExpr*, TypeSubst>;

struct SelectedCallTarget {
  ast::ModulePath module_path;
  const ast::ProcedureDecl* proc = nullptr;
};
using SelectedCallTargetMap =
    std::unordered_map<const ast::CallExpr*, SelectedCallTarget>;

enum class EntityKind {
  Value,
  Type,
  Class,
  ModuleAlias,
};

enum class EntitySource {
  Decl,
  Using,
  RegionAlias,
  Import,
};

struct Entity {
  EntityKind kind;
  std::optional<ast::ModulePath> origin_opt;
  std::optional<ast::Identifier> target_opt;
  EntitySource source;
  std::optional<core::Span> declaration_span;
  std::string language_symbol_id;
  std::vector<ast::TypeBound> type_param_class_bounds = {};
  std::vector<std::string> type_param_predicate_bounds = {};
};

using TypeDecl = std::variant<ast::RecordDecl,
                              ast::EnumDecl,
                              ast::ModalDecl,
                              ast::TypeAliasDecl>;

struct Sigma {
  std::vector<ast::ASTModule> mods;
  std::unordered_map<std::string, std::vector<core::Span>> unsafe_spans_by_file;
  std::map<PathKey, TypeDecl> types;
  std::map<PathKey, ast::ClassDecl> classes;
  std::map<PathKey, TypeRef> opaque_underlying_by_class_path;
};

using Scope = std::unordered_map<IdKey, Entity>;
using ScopeList = std::vector<Scope>;

struct ScopeContext {
  const project::Project* project = nullptr;
  std::optional<project::TargetProfile> target_profile;
  // Optional identity of the original Sigma source when sigma is copied into
  // this context for convenience (for example during codegen scope snapshots).
  // Callers may use this to preserve cache identity across equivalent copies.
  const Sigma* sigma_source = nullptr;
  Sigma sigma;
  mutable core::DiagnosticStream* diagnostics = nullptr;
  ExprTypeMap* expr_types = nullptr;
  DynamicRefineExprMap* dynamic_refine_checks = nullptr;
  GenericCallSubstMap* generic_call_substs = nullptr;
  SelectedCallTargetMap* selected_call_targets = nullptr;
  ast::ModulePath current_module;
  ScopeList scopes;
};

struct ResContext {
  const Sigma* sigma = nullptr;
  const ast::ModulePath* module = nullptr;
};

static inline void SpecDefsScopeContext() {
  SPEC_DEF("IdKeyRef", "5.1.1");
  SPEC_DEF("ScopeKey", "5.1.1");
  SPEC_DEF("Sigma", "5.1.1");
  SPEC_DEF("Gamma", "5.1.1");
  SPEC_DEF("EntityKind", "5.1.1");
  SPEC_DEF("EntitySource", "5.1.1");
  SPEC_DEF("Entity", "5.1.1");
  SPEC_DEF("Project", "5.1.1");
  SPEC_DEF("ResCtx", "5.1.1");
  SPEC_DEF("CurrentModule", "5.1.1");
  SPEC_DEF("Scopes", "5.1.1");
  SPEC_DEF("LocalScopes", "5.1.1");
  SPEC_DEF("ProcScope", "5.1.1");
  SPEC_DEF("ModuleScope", "5.1.1");
  SPEC_DEF("UniverseScope", "5.1.1");
  SPEC_DEF("UniverseProtectedRef", "5.1.1");
  SPEC_DEF("UniverseBindings", "5.1.1");
  SPEC_DEF("BytePrefix", "5.1.1");
  SPEC_DEF("Prefix", "5.1.1");
  SPEC_DEF("ReservedGen", "5.1.1");
  SPEC_DEF("ReservedCursive", "5.1.1");
  SPEC_DEF("ReservedId", "5.1.1");
  SPEC_DEF("ReservedModulePath", "5.1.1");
  SPEC_DEF("PrimTypeNames", "5.1.1");
  SPEC_DEF("SpecialTypeNames", "5.1.1");
  SPEC_DEF("AsyncTypeNames", "5.1.1");
  SPEC_DEF("PrimTypeKeys", "5.1.1");
  SPEC_DEF("SpecialTypeKeys", "5.1.1");
  SPEC_DEF("AsyncTypeKeys", "5.1.1");
  SPEC_DEF("KeywordKey", "5.1.1");
}

inline const project::Project* Project(const ScopeContext& ctx) {
  SpecDefsScopeContext();
  return ctx.project;
}

inline std::optional<project::TargetProfile> RequireSelectedTargetProfile(
    const ScopeContext& ctx) {
  SPEC_DEF("RequireSelectedTargetProfile", "1.6");
  return ctx.target_profile;
}

inline ResContext ResCtx(const ScopeContext& ctx) {
  SpecDefsScopeContext();
  return ResContext{&ctx.sigma, &ctx.current_module};
}

inline const ast::ModulePath& CurrentModule(const ScopeContext& ctx) {
  SpecDefsScopeContext();
  return ctx.current_module;
}

inline const ScopeList& Scopes(const ScopeContext& ctx) {
  SpecDefsScopeContext();
  return ctx.scopes;
}

inline ScopeList& Scopes(ScopeContext& ctx) {
  SpecDefsScopeContext();
  return ctx.scopes;
}

inline std::span<const Scope> LocalScopes(const ScopeList& scopes) {
  SpecDefsScopeContext();
  if (scopes.size() < 3) {
    return {};
  }
  return std::span<const Scope>(scopes.data(), scopes.size() - 3);
}

inline std::span<Scope> LocalScopes(ScopeList& scopes) {
  SpecDefsScopeContext();
  if (scopes.size() < 3) {
    return {};
  }
  return std::span<Scope>(scopes.data(), scopes.size() - 3);
}

inline const Scope& ProcScope(const ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 3];
}

inline Scope& ProcScope(ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 3];
}

inline const Scope& ModuleScope(const ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 2];
}

inline Scope& ModuleScope(ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 2];
}

inline const Scope& UniverseScope(const ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 1];
}

inline Scope& UniverseScope(ScopeList& scopes) {
  SpecDefsScopeContext();
  return scopes[scopes.size() - 1];
}

}  // namespace cursive::analysis
