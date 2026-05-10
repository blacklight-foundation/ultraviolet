// =============================================================================
// MIGRATION: item/generic_params.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 9: Generics
//   - Generic parameter syntax: <T; U>
//   - Type bounds: T <: ClassName
//   - Predicate bounds: where Bitcopy(T)
//   - Default type parameters
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/generics.cpp
//
// =============================================================================

#include "04_analysis/typing/type_decls.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/composite/classes.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

// =============================================================================
// SPEC DEFINITIONS
// =============================================================================

static inline void SpecDefsGenericParams() {
  SPEC_DEF("GenParams", "9.1");
  SPEC_DEF("GenArgs", "9.1");
  SPEC_DEF("GenBound", "9.2");
  SPEC_DEF("GenDefault", "9.3");
  SPEC_DEF("GenDistinct", "9.1");
  SPEC_DEF("GenWhere", "9.2");
}

}  // namespace

// =============================================================================
// EXPORTED: ProcessGenericParams
// =============================================================================

GenericParamsResult ProcessGenericParams(
    const ScopeContext& ctx,
    const std::vector<ast::TypeParam>& params) {
  SpecDefsGenericParams();
  GenericParamsResult result;
  result.ok = true;

  std::unordered_set<std::string> seen_names;

  for (const auto& param : params) {
    // Check for duplicate parameter names
    if (!seen_names.insert(param.name).second) {
      SPEC_RULE("GenDistinct-Err");
      result.ok = false;
      // §13.1.1 Diagnostics
      result.diag_id = "E-TYP-2304";
      return result;
    }

    GenericParamInfo info;
    info.name = param.name;

    // Process class bounds
    for (const auto& bound : param.bounds) {
      // Verify the class exists
      const auto key = PathKeyOf(bound.class_path);
      if (ctx.sigma.classes.find(key) == ctx.sigma.classes.end()) {
        SPEC_RULE("GenBound-ClassNotFound");
        result.ok = false;
        // §13.3 Constraints(1) + Diagnostics
        result.diag_id = "E-TYP-2305";
        return result;
      }
      for (const auto& arg : bound.generic_args) {
        const auto lowered_arg = LowerType(ctx, arg);
        if (!lowered_arg.ok) {
          result.ok = false;
          result.diag_id = lowered_arg.diag_id;
          return result;
        }
      }
      info.class_bounds.push_back(bound);
    }

    // Process default type if present
    if (param.default_type) {
      const auto lowered = LowerType(ctx, param.default_type);
      if (!lowered.ok) {
        result.ok = false;
        result.diag_id = lowered.diag_id;
        return result;
      }
      info.default_type = lowered.type;
    }

    result.params.push_back(info);
  }

  // Verify defaults come after non-defaults
  bool seen_default = false;
  for (const auto& info : result.params) {
    if (info.default_type) {
      seen_default = true;
    } else if (seen_default) {
      SPEC_RULE("GenDefault-Order-Err");
      result.ok = false;
      // Ill-formed generic parameter list (§13.1.1).
      result.diag_id = "E-TYP-2304";
      return result;
    }
  }

  SPEC_RULE("GenParams-Ok");
  return result;
}

GenericParamsResult ProcessGenericParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt) {
  return ProcessGenericParams(ctx, ast::TypeParamsOpt(params_opt));
}

// =============================================================================
// EXPORTED: CheckGenericArgs
// =============================================================================

GenericArgsCheckResult CheckGenericArgs(
    const ScopeContext& ctx,
    const std::vector<GenericParamInfo>& params,
    const std::vector<TypeRef>& args) {
  SpecDefsGenericParams();
  GenericArgsCheckResult result;
  result.ok = true;

  // Count required parameters (those without defaults)
  std::size_t required = 0;
  for (const auto& param : params) {
    if (!param.default_type) {
      ++required;
    }
  }

  // Check argument count
  if (args.size() < required) {
    SPEC_RULE("GenArgs-TooFew");
    result.ok = false;
    result.diag_id = "E-TYP-2303";
    return result;
  }
  if (args.size() > params.size()) {
    SPEC_RULE("GenArgs-TooMany");
    result.ok = false;
    result.diag_id = "E-TYP-2303";
    return result;
  }

  // Check each argument against its parameter's bounds
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& param = params[i];
    const auto& arg = args[i];

    // Check class bounds
    for (const auto& bound : param.class_bounds) {
      if (!TypeImplementsClass(ctx, arg, bound.class_path)) {
        SPEC_RULE("GenArgs-BoundNotSatisfied");
        result.ok = false;
        result.diag_id = "E-TYP-2302";
        return result;
      }
    }
  }

  SPEC_RULE("GenArgs-Ok");
  return result;
}

// =============================================================================
// EXPORTED: ApplyGenericSubstitution
// =============================================================================

TypeRef ApplyGenericSubstitution(
    const TypeRef& type,
    const std::vector<std::string>& param_names,
    const std::vector<TypeRef>& args) {
  SpecDefsGenericParams();

  if (!type) {
    return type;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePathType>) {
          // Check if this is a type parameter reference
          if (node.path.size() == 1) {
            for (std::size_t i = 0; i < param_names.size() && i < args.size(); ++i) {
              if (node.path[0] == param_names[i]) {
                return args[i];
              }
            }
          }
          // Not a type parameter, but may have generic args that need substitution
          if (node.generic_args.empty()) {
            return type;
          }
          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(ApplyGenericSubstitution(arg, param_names, args));
            }
            return MakeTypePath(node.path, new_args);
          } else if constexpr (std::is_same_v<T, TypeApply>) {
            std::vector<TypeRef> new_args;
            new_args.reserve(node.args.size());
            for (const auto& arg : node.args) {
              new_args.push_back(ApplyGenericSubstitution(arg, param_names, args));
            }
            return MakeTypeApply(node.path, new_args);
          } else if constexpr (std::is_same_v<T, TypePerm>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypePerm(node.perm, new_base);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          std::vector<TypeRef> new_members;
          new_members.reserve(node.members.size());
          for (const auto& member : node.members) {
            new_members.push_back(ApplyGenericSubstitution(member, param_names, args));
          }
          return MakeTypeUnion(new_members);
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          std::vector<TypeFuncParam> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.push_back({
                param.mode,
                ApplyGenericSubstitution(param.type, param_names, args)
            });
          }
          auto new_ret = ApplyGenericSubstitution(node.ret, param_names, args);
          return MakeTypeFunc(new_params, new_ret);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.emplace_back(
                param.first,
                ApplyGenericSubstitution(param.second, param_names, args));
          }
          auto new_ret = ApplyGenericSubstitution(node.ret, param_names, args);
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              SharedDep lowered;
              lowered.name = dep.name;
              lowered.type =
                  ApplyGenericSubstitution(dep.type, param_names, args);
              deps.push_back(std::move(lowered));
            }
            deps_opt = std::move(deps);
          }
          return MakeTypeClosure(std::move(new_params), new_ret,
                                 std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          std::vector<TypeRef> new_elements;
          new_elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            new_elements.push_back(ApplyGenericSubstitution(elem, param_names, args));
          }
          return MakeTypeTuple(new_elements);
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          auto new_elem = ApplyGenericSubstitution(node.element, param_names, args);
          return MakeTypeArray(new_elem, node.length, node.length_expr_text);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          auto new_elem = ApplyGenericSubstitution(node.element, param_names, args);
          return MakeTypeSlice(new_elem);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          auto new_elem = ApplyGenericSubstitution(node.element, param_names, args);
          return MakeTypePtr(new_elem, node.state);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          auto new_elem = ApplyGenericSubstitution(node.element, param_names, args);
          return MakeTypeRawPtr(node.qual, new_elem);
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(ApplyGenericSubstitution(arg, param_names, args));
          }
          return MakeTypeModalState(node.path, node.state, new_args);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRefine(new_base, node.predicate);
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRange(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRangeInclusive(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRangeFrom(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRangeTo(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          auto new_base = ApplyGenericSubstitution(node.base, param_names, args);
          return MakeTypeRangeToInclusive(new_base);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return MakeTypeRangeFull();
        } else {
          // TypePrim, TypeString, TypeBytes, TypeDynamic, TypeOpaque
          return type;
        }
      },
      type->node);
}

// TypeImplementsClass is defined in classes.cpp and declared in classes.h
// Note: The removed duplicate had extra handling for primitive types implementing
// Bitcopy, Clone, and Eq. This functionality should be in classes.cpp if needed.

}  // namespace cursive::analysis
