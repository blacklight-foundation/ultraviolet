/*
 * =============================================================================
 * monomorphize.cpp - Generic Monomorphization Implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - Docs/SPECIFICATION.md, Section 13.1.4 "Monomorphization" (lines 22529-22565)
 *   - Docs/SPECIFICATION.md, Section 13.1.3 "Generic Instantiation"
 *   - Docs/SPECIFICATION.md, Section 13.4 "Class Constraints"
 *
 * This file implements the monomorphization system for Ultraviolet generics:
 *   - Type instantiation with concrete type arguments
 *   - Type substitution mapping
 *   - Type argument inference from call sites
 *   - Bound checking for type constraints
 *
 * MONOMORPHIZATION PROCESS:
 *   1. Collect type arguments (explicit or inferred)
 *   2. Build substitution: param -> arg
 *   3. Substitute all type params in:
 *      - Field types
 *      - Method signatures
 *      - Where clause bounds
 *   4. Validate bounds are satisfied
 *   5. Generate specialized code
 *
 * =============================================================================
 */

#include "04_analysis/generics/monomorphize.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/typing/type_equiv.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsMonomorphize() {
  SPEC_DEF("Instantiate", "UVX.13.1.4");
  SPEC_DEF("InstGraph", "UVX.13.1.4");
  SPEC_DEF("Monomorphize", "UVX.13.1.4");
  SPEC_DEF("TypeSubst", "UVX.13.1.4");
  SPEC_DEF("DefaultArgs", "14.1.4");
  SPEC_DEF("ModalRefSubst", "13.1.3");
}

// Compare type keys for ordering
bool TypeKeyLessForInst(const TypeRef& a, const TypeRef& b) {
  if (!a && !b) return false;
  if (!a) return true;
  if (!b) return false;
  return TypeKeyLess(TypeKeyOf(a), TypeKeyOf(b));
}

bool IsTypeParamName(const std::vector<ast::TypeParam>& params, std::string_view name) {
  for (const auto& param : params) {
    if (param.name == name) {
      return true;
    }
  }
  return false;
}

bool TypePathEqLocal(const TypePath& lhs, const TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

template <typename PathT>
std::string JoinPath(const PathT& path) {
  std::string out;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      out += "::";
    }
    out += path[i];
  }
  return out;
}

TypeRef StripPermLayers(const TypeRef& type) {
  TypeRef current = type;
  while (current) {
    const auto* perm = std::get_if<TypePerm>(&current->node);
    if (!perm) {
      break;
    }
    current = perm->base;
  }
  return current;
}

bool CanSatisfyClassBoundByShape(const TypeRef& type) {
  const TypeRef stripped = StripPermLayers(type);
  if (!stripped) {
    return false;
  }

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        return std::is_same_v<T, TypePathType> ||
               std::is_same_v<T, TypeOpaque> ||
               std::is_same_v<T, TypeDynamic> ||
               std::is_same_v<T, TypeModalState>;
      },
      stripped->node);
}

struct LowerDefaultTypeResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

core::Diagnostic MakeInternalGenericDiagnostic(
    core::Severity severity,
    const std::optional<core::Span>& span,
    const std::string& message) {
  core::Diagnostic diag;
  diag.severity = severity;
  diag.span = span;
  diag.message = message;
  return diag;
}

LowerDefaultTypeResult UnsupportedDefaultTypeResult() {
  LowerDefaultTypeResult result;
  result.ok = false;
  result.type = nullptr;
  return result;
}

bool ContainsTypeParamForInference(const std::vector<ast::TypeParam>& params,
                                   const TypeRef& type) {
  if (!type) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&type->node)) {
    if (path->path.size() == 1 && IsTypeParamName(params, path->path[0])) {
      return true;
    }
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return ContainsTypeParamForInference(params, node.base);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (ContainsTypeParamForInference(params, elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return ContainsTypeParamForInference(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return ContainsTypeParamForInference(params, node.element);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return ContainsTypeParamForInference(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return ContainsTypeParamForInference(params, node.element);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (const auto& member : node.members) {
            if (ContainsTypeParamForInference(params, member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            if (ContainsTypeParamForInference(params, param.type)) {
              return true;
            }
          }
          return ContainsTypeParamForInference(params, node.ret);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          for (const auto& arg : node.generic_args) {
            if (ContainsTypeParamForInference(params, arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            if (ContainsTypeParamForInference(params, arg)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return ContainsTypeParamForInference(params, node.base);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& param : node.params) {
            if (ContainsTypeParamForInference(params, param.second)) {
              return true;
            }
          }
          if (ContainsTypeParamForInference(params, node.ret)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (ContainsTypeParamForInference(params, dep.type)) {
                return true;
              }
            }
          }
          return false;
        }
        return false;
      },
      type->node);
}

bool BindTypeParamsForInference(const std::vector<ast::TypeParam>& params,
                                const TypeRef& expected,
                                const TypeRef& actual,
                                std::map<std::string, TypeRef>& bindings) {
  if (!expected || !actual) {
    return false;
  }

  if (const auto* path = std::get_if<TypePathType>(&expected->node)) {
    if (path->path.size() == 1 && IsTypeParamName(params, path->path[0])) {
      const std::string name = path->path[0];
      const auto it = bindings.find(name);
      if (it == bindings.end()) {
        bindings.emplace(name, actual);
        return true;
      }
      const auto equiv = TypeEquiv(it->second, actual);
      return equiv.ok && equiv.equiv;
    }
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          const auto* other = std::get_if<TypePerm>(&actual->node);
          return other && node.perm == other->perm &&
                 BindTypeParamsForInference(params, node.base, other->base, bindings);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          const auto* other = std::get_if<TypeTuple>(&actual->node);
          if (!other || node.elements.size() != other->elements.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (!BindTypeParamsForInference(params, node.elements[i],
                                            other->elements[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          const auto* other = std::get_if<TypeArray>(&actual->node);
          return other && node.length == other->length &&
                 BindTypeParamsForInference(params, node.element, other->element, bindings);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          const auto* other = std::get_if<TypeSlice>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.element, other->element, bindings);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          const auto* other = std::get_if<TypePtr>(&actual->node);
          return other && node.state == other->state &&
                 BindTypeParamsForInference(params, node.element, other->element, bindings);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto* other = std::get_if<TypeRawPtr>(&actual->node);
          return other && node.qual == other->qual &&
                 BindTypeParamsForInference(params, node.element, other->element, bindings);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          const auto* other = std::get_if<TypeUnion>(&actual->node);
          if (!other || node.members.size() != other->members.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.members.size(); ++i) {
            if (!BindTypeParamsForInference(params, node.members[i],
                                            other->members[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          const auto* other = std::get_if<TypeFunc>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].mode != other->params[i].mode) {
              return false;
            }
            if (!BindTypeParamsForInference(params, node.params[i].type,
                                            other->params[i].type, bindings)) {
              return false;
            }
          }
          return BindTypeParamsForInference(params, node.ret, other->ret, bindings);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          const auto* other = std::get_if<TypePathType>(&actual->node);
          if (!other || !TypePathEqLocal(node.path, other->path) ||
              node.generic_args.size() != other->generic_args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
            if (!BindTypeParamsForInference(params, node.generic_args[i],
                                            other->generic_args[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          const auto* other = std::get_if<TypeModalState>(&actual->node);
          if (!other || !TypePathEqLocal(node.path, other->path) ||
              node.state != other->state ||
              node.generic_args.size() != other->generic_args.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
            if (!BindTypeParamsForInference(params, node.generic_args[i],
                                            other->generic_args[i], bindings)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          const auto* other = std::get_if<TypeDynamic>(&actual->node);
          return other && TypePathEqLocal(node.path, other->path);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          const auto* other = std::get_if<TypeRefine>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base, bindings);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          const auto* other = std::get_if<TypeClosure>(&actual->node);
          if (!other || node.params.size() != other->params.size()) {
            return false;
          }
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (node.params[i].first != other->params[i].first) {
              return false;
            }
            if (!BindTypeParamsForInference(params, node.params[i].second,
                                            other->params[i].second, bindings)) {
              return false;
            }
          }
          return BindTypeParamsForInference(params, node.ret, other->ret, bindings);
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          const auto* other = std::get_if<TypeOpaque>(&actual->node);
          return other && TypePathEqLocal(node.class_path, other->class_path);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          const auto* other = std::get_if<TypeString>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          const auto* other = std::get_if<TypeBytes>(&actual->node);
          return other && node.state == other->state;
        } else if constexpr (std::is_same_v<T, TypePrim>) {
          const auto* other = std::get_if<TypePrim>(&actual->node);
          return other && node.name == other->name;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          const auto* other = std::get_if<TypeRange>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base,
                                            bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          const auto* other = std::get_if<TypeRangeInclusive>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base,
                                            bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          const auto* other = std::get_if<TypeRangeFrom>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base,
                                            bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          const auto* other = std::get_if<TypeRangeTo>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base,
                                            bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          const auto* other = std::get_if<TypeRangeToInclusive>(&actual->node);
          return other &&
                 BindTypeParamsForInference(params, node.base, other->base,
                                            bindings);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return std::holds_alternative<TypeRangeFull>(actual->node);
        }
        const auto equiv = TypeEquiv(expected, actual);
        return equiv.ok && equiv.equiv;
      },
      expected->node);
}

}  // namespace

bool InstantiationKey::operator<(const InstantiationKey& other) const {
  if (decl_path < other.decl_path) return true;
  if (other.decl_path < decl_path) return false;

  if (args.size() != other.args.size()) {
    return args.size() < other.args.size();
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (TypeKeyLessForInst(args[i], other.args[i])) return true;
    if (TypeKeyLessForInst(other.args[i], args[i])) return false;
  }

  return false;
}

bool InstantiationKey::operator==(const InstantiationKey& other) const {
  if (decl_path != other.decl_path) return false;
  if (args.size() != other.args.size()) return false;

  for (std::size_t i = 0; i < args.size(); ++i) {
    auto equiv = TypeEquiv(args[i], other.args[i]);
    if (!equiv.equiv) return false;
  }

  return true;
}

void MonomorphizeContext::Demand(const InstantiationKey& key) {
  SpecDefsMonomorphize();
  SPEC_RULE("Mono-Demand");

  auto it = instantiations_.find(key);
  if (it != instantiations_.end()) {
    return;  // Already demanded
  }

  InstantiationEntry entry;
  entry.key = key;
  entry.processed = false;
  instantiations_[key] = entry;
  worklist_.push_back(key);
}

bool MonomorphizeContext::HasInstantiation(const InstantiationKey& key) const {
  return instantiations_.find(key) != instantiations_.end();
}

bool MonomorphizeContext::ProcessToFixedPoint() {
  SpecDefsMonomorphize();
  SPEC_RULE("Mono-FixedPoint");

  // SPEC: Docs/SPECIFICATION.md Section 13.1.4 "Recursion Depth"
  // "The maximum instantiation depth is 128."

  while (!worklist_.empty()) {
    if (current_depth_ >= kMaxDepth) {
      // Non-terminating type-level recursion
      // SPEC: "Implementations MUST detect and reject infinite monomorphization recursion."
      SPEC_RULE("Mono-NonTerminating");
      return false;
    }

    InstantiationKey key = worklist_.back();
    worklist_.pop_back();

    auto it = instantiations_.find(key);
    if (it == instantiations_.end() || it->second.processed) {
      continue;
    }

    ++current_depth_;

    // Mark as processed
    it->second.processed = true;

    // Dependencies are discovered by declaration-level monomorphization passes.
    // This fixed-point worklist only tracks instantiation depth/termination.

    --current_depth_;
  }

  return true;
}

TypeRef InstantiateType(const TypeRef& type, const TypeSubst& subst) {
  SpecDefsMonomorphize();
  SPEC_RULE("Instantiate-Type");

  // SPEC: Docs/SPECIFICATION.md Section 13.1.4
  // "Given a generic declaration D<T_1, ..., T_n> and concrete type arguments
  //  A_1, ..., A_n, monomorphization produces a specialized declaration
  //  D[A_1/T_1, ..., A_n/T_n] where each occurrence of T_i in the body is
  //  replaced with A_i."

  if (!type) {
    return type;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<T, TypePathType>) {
            // Check if this is a type parameter
          if (node.path.size() == 1) {
            auto it = subst.find(node.path[0]);
            if (it != subst.end()) {
              return it->second;
            }
          }

          // Instantiate generic args if any
          if (node.generic_args.empty()) {
            return type;
          }

          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(InstantiateType(arg, subst));
          }

            TypePathType new_node = node;
            new_node.generic_args = std::move(new_args);
            return MakeType(new_node);
          }
          else if constexpr (std::is_same_v<T, TypeApply>) {
            std::vector<TypeRef> new_args;
            new_args.reserve(node.args.size());
            for (const auto& arg : node.args) {
              new_args.push_back(InstantiateType(arg, subst));
            }
            return MakeTypeApply(node.path, std::move(new_args));
          }
          else if constexpr (std::is_same_v<T, TypePerm>) {
          return MakeTypePerm(node.perm, InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeTuple>) {
          std::vector<TypeRef> new_elems;
          new_elems.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            new_elems.push_back(InstantiateType(elem, subst));
          }
          return MakeTypeTuple(std::move(new_elems));
        }
        else if constexpr (std::is_same_v<T, TypeArray>) {
          return MakeTypeArray(InstantiateType(node.element, subst),
                               node.length,
                               node.length_expr_text);
        }
        else if constexpr (std::is_same_v<T, TypeSlice>) {
          return MakeTypeSlice(InstantiateType(node.element, subst));
        }
        else if constexpr (std::is_same_v<T, TypePtr>) {
          return MakeTypePtr(InstantiateType(node.element, subst), node.state);
        }
        else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return MakeTypeRawPtr(node.qual, InstantiateType(node.element, subst));
        }
        else if constexpr (std::is_same_v<T, TypeUnion>) {
          std::vector<TypeRef> new_members;
          new_members.reserve(node.members.size());
          for (const auto& member : node.members) {
            new_members.push_back(InstantiateType(member, subst));
          }
          return MakeTypeUnion(std::move(new_members));
        }
        else if constexpr (std::is_same_v<T, TypeFunc>) {
          std::vector<TypeFuncParam> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.push_back(TypeFuncParam{
                param.mode,
                InstantiateType(param.type, subst)});
          }
          return MakeTypeFunc(std::move(new_params),
                              InstantiateType(node.ret, subst));
        }
        else if constexpr (std::is_same_v<T, TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> new_params;
          new_params.reserve(node.params.size());
          for (const auto& param : node.params) {
            new_params.emplace_back(param.first,
                                    InstantiateType(param.second, subst));
          }
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              SharedDep lowered;
              lowered.name = dep.name;
              lowered.type = InstantiateType(dep.type, subst);
              deps.push_back(std::move(lowered));
            }
            deps_opt = std::move(deps);
          }
          return MakeTypeClosure(std::move(new_params),
                                 InstantiateType(node.ret, subst),
                                 std::move(deps_opt));
        }
        else if constexpr (std::is_same_v<T, TypeModalState>) {
          std::vector<TypeRef> new_args;
          new_args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            new_args.push_back(InstantiateType(arg, subst));
          }
          return MakeTypeModalState(node.path, node.state, std::move(new_args));
        }
        else if constexpr (std::is_same_v<T, TypeRefine>) {
          // Instantiate the base type of refinement types
          return MakeTypeRefine(InstantiateType(node.base, subst), node.predicate);
        }
        else if constexpr (std::is_same_v<T, TypeRange>) {
          return MakeTypeRange(InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          return MakeTypeRangeInclusive(InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          return MakeTypeRangeFrom(InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          return MakeTypeRangeTo(InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          return MakeTypeRangeToInclusive(InstantiateType(node.base, subst));
        }
        else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return MakeTypeRangeFull();
        }
        else {
          // TypePrim, TypeString, TypeBytes, TypeDynamic, TypeOpaque
          // These do not contain type variables
          return type;
        }
      },
      type->node);
}

// Simple lowering of default type arguments (handles common primitive defaults).
// If lowering cannot represent a parsed AST shape, this returns an internal
// failure with no spec diagnostic id.
static LowerDefaultTypeResult LowerDefaultType(
    const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return UnsupportedDefaultTypeResult();
  }

  return std::visit(
      [&](const auto& node) -> LowerDefaultTypeResult {
        using T = std::decay_t<decltype(node)>;
        LowerDefaultTypeResult result;

        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          result.type = MakeTypePrim(node.name);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          TypePath path;
          path.reserve(node.path.size());
          for (const auto& ident : node.path) {
            path.push_back(ident);
          }

          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LowerDefaultType(arg);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            args.push_back(lowered.type);
          }

          result.type = node.generic_args.empty()
                            ? MakeTypePath(std::move(path))
                            : MakeTypePath(std::move(path), std::move(args));
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto lowered = LowerDefaultType(elem);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            elements.push_back(lowered.type);
          }
          result.type = MakeTypeTuple(std::move(elements));
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          const auto elem = LowerDefaultType(node.element);
          if (!elem.ok || !elem.type) {
            return elem;
          }
          // Array length expressions require const-eval context; preserve the
          // element type and use length 0 at this monomorphization stage.
          result.type = MakeTypeArray(elem.type, 0);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          const auto elem = LowerDefaultType(node.element);
          if (!elem.ok || !elem.type) {
            return elem;
          }
          result.type = MakeTypeSlice(elem.type);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          const auto base = LowerDefaultType(node.base);
          if (!base.ok || !base.type) {
            return base;
          }
          Permission perm = Permission::Const;
          switch (node.perm) {
            case ast::TypePerm::Const:
              perm = Permission::Const;
              break;
            case ast::TypePerm::Unique:
              perm = Permission::Unique;
              break;
            case ast::TypePerm::Shared:
              perm = Permission::Shared;
              break;
          }
          result.type = MakeTypePerm(perm, base.type);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& member : node.types) {
            const auto lowered = LowerDefaultType(member);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            members.push_back(lowered.type);
          }
          result.type = MakeTypeUnion(std::move(members));
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerDefaultType(param.type);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            std::optional<ParamMode> mode;
            if (param.mode.has_value()) {
              mode = ParamMode::Move;
            }
            params.push_back(TypeFuncParam{mode, lowered.type});
          }

          const auto ret = LowerDefaultType(node.ret);
          if (!ret.ok || !ret.type) {
            return ret;
          }

          result.type = MakeTypeFunc(std::move(params), ret.type);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerDefaultType(param.type);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            const bool is_move = param.mode.has_value();
            params.emplace_back(is_move, lowered.type);
          }

          const auto ret = LowerDefaultType(node.ret);
          if (!ret.ok || !ret.type) {
            return ret;
          }

          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto dep_type = LowerDefaultType(dep.type);
              if (!dep_type.ok || !dep_type.type) {
                return dep_type;
              }
              SharedDep lowered_dep;
              lowered_dep.name = dep.name;
              lowered_dep.type = dep_type.type;
              deps.push_back(std::move(lowered_dep));
            }
            deps_opt = std::move(deps);
          }

          result.type = MakeTypeClosure(std::move(params), ret.type,
                                        std::move(deps_opt));
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          const auto elem = LowerDefaultType(node.element);
          if (!elem.ok || !elem.type) {
            return elem;
          }
          std::optional<PtrState> state;
          if (node.state.has_value()) {
            switch (*node.state) {
              case ast::PtrState::Valid:
                state = PtrState::Valid;
                break;
              case ast::PtrState::Null:
                state = PtrState::Null;
                break;
              case ast::PtrState::Expired:
                state = PtrState::Expired;
                break;
            }
          }
          result.type = MakeTypePtr(elem.type, state);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          const auto elem = LowerDefaultType(node.element);
          if (!elem.ok || !elem.type) {
            return elem;
          }
          const RawPtrQual qual = node.qual == ast::RawPtrQual::Imm
                                      ? RawPtrQual::Imm
                                      : RawPtrQual::Mut;
          result.type = MakeTypeRawPtr(qual, elem.type);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          std::optional<StringState> state;
          if (node.state.has_value()) {
            state = *node.state == ast::StringState::Managed
                        ? StringState::Managed
                        : StringState::View;
          }
          result.type = MakeTypeString(state);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          std::optional<BytesState> state;
          if (node.state.has_value()) {
            state = *node.state == ast::BytesState::Managed
                        ? BytesState::Managed
                        : BytesState::View;
          }
          result.type = MakeTypeBytes(state);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          result.type = MakeTypeDynamic(node.path);
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LowerDefaultType(arg);
            if (!lowered.ok || !lowered.type) {
              return lowered;
            }
            args.push_back(lowered.type);
          }
          result.type = MakeTypeModalState(node.path, node.state, std::move(args));
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          result.type = MakeTypeOpaque(node.path, nullptr, core::Span{});
          return result;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          const auto base = LowerDefaultType(node.base);
          if (!base.ok || !base.type) {
            return base;
          }
          result.type = MakeTypeRefine(base.type, node.predicate);
          return result;
        } else {
          return UnsupportedDefaultTypeResult();
        }
      },
      type->node);
}

TypeSubst BuildSubstitution(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args) {
  SpecDefsMonomorphize();
  SPEC_RULE("Build-Subst");

  TypeSubst subst;

  for (std::size_t i = 0; i < params.size() && i < args.size(); ++i) {
    subst[params[i].name] = args[i];
  }

  // Fill in defaults for missing args
  // SPEC: Docs/SPECIFICATION.md Section 14.1.4 "DefaultArgs"
  // Each default is substituted through the already-expanded argument prefix:
  // A_i' = TypeSubst([A_1'/P_1.name, ...], T_i).
  for (std::size_t i = args.size(); i < params.size(); ++i) {
    if (params[i].default_type) {
      const auto default_arg = LowerDefaultType(params[i].default_type);
      if (default_arg.ok && default_arg.type) {
        const auto substituted = InstantiateType(default_arg.type, subst);
        subst[params[i].name] =
            substituted ? substituted : MakeTypePrim("!");
      } else {
        // Preserve a concrete substitution entry so failure is not silent.
        // This invalid never-type forces subsequent checks to fail deterministically.
        subst[params[i].name] = MakeTypePrim("!");
      }
    }
  }

  return subst;
}

TypeSubst BuildModalRefSubstitution(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args) {
  SpecDefsMonomorphize();
  SPEC_RULE("ModalRefSubst");

  return BuildSubstitution(params, args);
}

BoundCheckResult CheckBoundsSatisfied(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args) {
  SpecDefsMonomorphize();
  SPEC_RULE("Check-Bounds");

  // SPEC: Docs/SPECIFICATION.md Section 13.4.1 "Constraint Satisfaction"
  // "A generic instantiation is well-formed only if every constrained
  //  parameter T <: Cl is instantiated with a type that implements Cl."

  BoundCheckResult result;

  // For each parameter with bounds, check the corresponding argument satisfies them.
  // This check is context-free: it verifies only what can be determined from the
  // available parameter/argument representations.
  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto& param = params[i];
    if (param.bounds.empty()) {
      continue;
    }

    TypeRef arg;
    if (i < args.size()) {
      arg = args[i];
    } else if (param.default_type) {
      const auto lowered_default = LowerDefaultType(param.default_type);
      if (!lowered_default.ok || !lowered_default.type) {
        result.ok = false;
        result.diag_id = lowered_default.diag_id;
        result.param_name = param.name;
        if (result.diag_id.has_value()) {
          if (auto diag = core::MakeDiagnosticById(result.diag_id.value(), param.span)) {
            diag->message = "Unsupported default type for bounded parameter '" +
                            param.name + "'";
            result.diagnostics.push_back(std::move(*diag));
          } else {
            result.diagnostics.push_back(MakeInternalGenericDiagnostic(
                core::Severity::Error, param.span,
                "Internal error: unresolved diagnostic id '" +
                    std::string(result.diag_id.value()) + "'"));
          }
        } else {
          result.diagnostics.push_back(MakeInternalGenericDiagnostic(
              core::Severity::Error, param.span,
              "Internal error: unable to lower default type for bounded parameter '" +
                  param.name + "'"));
        }
        return result;
      }
      arg = lowered_default.type;
    }

    if (!arg) {
      result.ok = false;
      result.diag_id = "E-TYP-2303";
      result.param_name = param.name;
      if (auto diag = core::MakeDiagnosticById("E-TYP-2303", param.span)) {
        diag->message =
            "Missing type argument for bounded generic parameter '" +
            param.name + "'";
        result.diagnostics.push_back(std::move(*diag));
      } else {
        result.diagnostics.push_back(MakeInternalGenericDiagnostic(
            core::Severity::Error, param.span,
            "Internal error: unresolved diagnostic id 'E-TYP-2303'"));
      }
      return result;
    }

    for (const auto& bound : param.bounds) {
      if (bound.class_path.empty()) {
        result.ok = false;
        result.diag_id = "E-TYP-2305";
        result.param_name = param.name;
        result.type_name = TypeToString(arg);
        if (auto diag = core::MakeDiagnosticById("E-TYP-2305", param.span)) {
          diag->message = "Class bound for parameter '" + param.name +
                          "' is empty or malformed";
          result.diagnostics.push_back(std::move(*diag));
        } else {
          result.diagnostics.push_back(MakeInternalGenericDiagnostic(
              core::Severity::Error, param.span,
              "Internal error: unresolved diagnostic id 'E-TYP-2305'"));
        }
        return result;
      }

      if (!CanSatisfyClassBoundByShape(arg)) {
        result.ok = false;
        result.diag_id = "E-TYP-2302";
        result.param_name = param.name;
        result.type_name = TypeToString(arg);
        result.bound_name = JoinPath(bound.class_path);
        if (auto diag = core::MakeDiagnosticById("E-TYP-2302", param.span)) {
          diag->message = "Type '" + result.type_name +
                          "' cannot satisfy class bound '" +
                          result.bound_name + "' for parameter '" +
                          param.name + "'";
          result.diagnostics.push_back(std::move(*diag));
        } else {
          result.diagnostics.push_back(MakeInternalGenericDiagnostic(
              core::Severity::Error, param.span,
              "Internal error: unresolved diagnostic id 'E-TYP-2302'"));
        }
        return result;
      }
    }
  }

  return result;
}

InferResult InferTypeArguments(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& expected_param_types,
    const std::vector<TypeRef>& actual_arg_types,
    const std::optional<TypeRef>& expected_return) {
  SpecDefsMonomorphize();
  SPEC_RULE("Infer-TypeArgs");

  // SPEC: Docs/SPECIFICATION.md Section 13.1.3 "Type Inference"
  // Type arguments can be inferred from value arguments at call sites.

  InferResult result;
  result.inferred_args.resize(params.size());

  std::map<std::string, TypeRef> bindings;

  for (std::size_t i = 0; i < expected_param_types.size() &&
                          i < actual_arg_types.size(); ++i) {
    const auto& expected = expected_param_types[i];
    const auto& actual = actual_arg_types[i];

    if (!expected || !actual) {
      continue;
    }

    const bool contains_type_param =
        ContainsTypeParamForInference(params, expected);
    const bool matched =
        BindTypeParamsForInference(params, expected, actual, bindings);

    if (contains_type_param && !matched) {
      // Inference mismatch involving type parameters.
      result.ok = false;
      result.diag_id = "E-TYP-2301";
      return result;
    }
  }

  // expected_return is accepted for API stability; current Ultraviolet inference
  // is argument-driven and does not use contextual return constraints.
  (void)expected_return;

  for (std::size_t i = 0; i < params.size(); ++i) {
    const auto it = bindings.find(params[i].name);
    if (it != bindings.end()) {
      result.inferred_args[i] = it->second;
      continue;
    }

    if (params[i].default_type) {
      const auto lowered_default = LowerDefaultType(params[i].default_type);
      if (!lowered_default.ok || !lowered_default.type) {
        result.ok = false;
        result.diag_id = lowered_default.diag_id;
        return result;
      }
      result.inferred_args[i] = lowered_default.type;
      continue;
    }

    // Could not infer and no default.
    result.ok = false;
    result.diag_id = "E-TYP-2301";
  }

  return result;
}

}  // namespace ultraviolet::analysis
