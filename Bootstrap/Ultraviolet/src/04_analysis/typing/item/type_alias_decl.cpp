// =============================================================================
// MIGRATION: item/type_alias_decl.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   Section 5.3.6: Type Alias Declarations
//   - Type alias grammar
//   - Generic type aliases
//   - Type refinements (where { predicate })
//
// SOURCE: ultraviolet-bootstrap/src/03_analysis/types/type_decls.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_wf.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/contracts/contract_check.h"
#include "04_analysis/generics/monomorphize.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsTypeAliasDecl() {
  SPEC_DEF("WF-TypeAlias", "5.3.6");
  SPEC_DEF("TypeAlias-Cycle", "5.3.6");
  SPEC_DEF("TypeAlias-Refine", "5.3.6");
}

// =============================================================================
// HELPERS
// =============================================================================

// Lower type with well-formedness check
static LowerTypeResult LowerTypeWithWF(const ScopeContext& ctx,
                                       const std::shared_ptr<ast::Type>& type) {
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    return lowered;
  }
  const auto wf = TypeWF(ctx, lowered.type);
  if (!wf.ok) {
    return {false, wf.diag_id, {}};
  }
  return lowered;
}

// Collect type alias dependencies for cycle detection
static void CollectAliasDeps(const ScopeContext& ctx,
                             const std::shared_ptr<ast::Type>& type,
                             std::vector<PathKey>& deps) {
  if (!type) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          const auto key = PathKeyOf(ast::Path(node.path.begin(), node.path.end()));
          const auto it = ctx.sigma.types.find(key);
          if (it != ctx.sigma.types.end() &&
              std::holds_alternative<ast::TypeAliasDecl>(it->second)) {
            deps.push_back(key);
          }
          // Also check generic args
          for (const auto& arg : node.generic_args) {
            CollectAliasDeps(ctx, arg, deps);
          }
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          CollectAliasDeps(ctx, node.base, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& elem : node.types) {
            CollectAliasDeps(ctx, elem, deps);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            CollectAliasDeps(ctx, param.type, deps);
          }
          CollectAliasDeps(ctx, node.ret, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            CollectAliasDeps(ctx, param.type, deps);
          }
          CollectAliasDeps(ctx, node.ret, deps);
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              CollectAliasDeps(ctx, dep.type, deps);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            CollectAliasDeps(ctx, elem, deps);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          CollectAliasDeps(ctx, node.element, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          CollectAliasDeps(ctx, node.base, deps);
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            CollectAliasDeps(ctx, arg, deps);
          }
        }
        // TypePrim, TypeString, TypeBytes, TypeDynamic - no dependencies
      },
      type->node);
}

// Check for type alias cycles
static bool TypeAliasCycleFrom(const ScopeContext& ctx,
                               const PathKey& start,
                               std::set<PathKey>& active,
                               std::set<PathKey>& done) {
  if (done.find(start) != done.end()) {
    return false;
  }
  if (!active.insert(start).second) {
    return true;  // Cycle detected
  }

  const auto it = ctx.sigma.types.find(start);
  if (it == ctx.sigma.types.end()) {
    active.erase(start);
    done.insert(start);
    return false;
  }
  const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second);
  if (!alias) {
    active.erase(start);
    done.insert(start);
    return false;
  }

  std::vector<PathKey> deps;
  CollectAliasDeps(ctx, alias->type, deps);
  for (const auto& dep : deps) {
    if (TypeAliasCycleFrom(ctx, dep, active, done)) {
      return true;
    }
  }

  active.erase(start);
  done.insert(start);
  return false;
}

// Check if type alias has a cycle
static bool TypeAliasOk(const ScopeContext& ctx, const ast::Path& path) {
  std::set<PathKey> active;
  std::set<PathKey> done;
  return !TypeAliasCycleFrom(ctx, PathKeyOf(path), active, done);
}

}  // namespace

// =============================================================================
// EXPORTED: TypeTypeAliasDecl
// =============================================================================

TypeAliasDeclResult TypeTypeAliasDecl(
    const ScopeContext& ctx,
    const ast::TypeAliasDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags) {
  SpecDefsTypeAliasDecl();
  TypeAliasDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::TypeAlias);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Build type path
  ast::Path type_path;
  for (const auto& seg : module_path) {
    type_path.push_back(seg);
  }
  type_path.push_back(decl.name);

  // Check for cycles
  if (!TypeAliasOk(ctx, type_path)) {
    SPEC_RULE("TypeAlias-Cycle-Err");
    result.ok = false;
    result.diag_id = "TypeAlias-Recursive-Err";
    return result;
  }

  // Process generic parameters
  GenericParamsResult gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  for (const auto& gp : gen_params.params) {
    result.generic_params.push_back(gp.name);
  }

  // Process where clauses
  std::vector<std::string> type_param_names;
  for (const auto& gp : gen_params.params) {
    type_param_names.push_back(gp.name);
  }
  if (decl.predicate_clause_opt.has_value()) {
    const auto where_result = ProcessWhereClause(
        ctx, *decl.predicate_clause_opt, type_param_names);
    if (!where_result.ok) {
      result.ok = false;
      result.diag_id = where_result.diag_id;
      return result;
    }
  }

  // Lower the aliased type
  const auto lowered = LowerTypeWithWF(ctx, decl.type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }
  result.aliased_type = lowered.type;

  // Refinement predicates are handled through the type system
  // (e.g., via TypeRefinement nodes in the type AST)
  // For type aliases, refinements would be part of the aliased type itself

  SPEC_RULE("WF-TypeAlias-Ok");
  return result;
}

// =============================================================================
// EXPORTED: TypeTypeAliasDeclSignature (first pass)
// =============================================================================

TypeAliasDeclResult TypeTypeAliasDeclSignature(
    const ScopeContext& ctx,
    const ast::TypeAliasDecl& decl,
    const ast::ModulePath& module_path) {
  SpecDefsTypeAliasDecl();
  TypeAliasDeclResult result;
  result.ok = true;

  const auto attr_validation =
      ValidateAttributes(decl.attrs, AttributeTarget::TypeAlias);
  if (!attr_validation.ok) {
    result.ok = false;
    result.diag_id = attr_validation.diag_id;
    return result;
  }

  // Process generic parameters
  const auto gen_params = ProcessGenericParams(ctx, decl.generic_params);
  if (!gen_params.ok) {
    result.ok = false;
    result.diag_id = gen_params.diag_id;
    return result;
  }
  for (const auto& gp : gen_params.params) {
    result.generic_params.push_back(gp.name);
  }

  // Lower the aliased type
  const auto lowered = LowerTypeWithWF(ctx, decl.type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }
  result.aliased_type = lowered.type;
  result.has_refinement = false;  // Refinements handled through type system

  (void)module_path;

  SPEC_RULE("WF-TypeAlias-Sig-Ok");
  return result;
}

}  // namespace ultraviolet::analysis
