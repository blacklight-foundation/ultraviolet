// =============================================================================
// MIGRATION: typing/type_predicates.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 9.2: Type Predicates
//   - Bitcopy, Clone, Drop, FfiSafe predicates
//   - Structural derivation for composite types
//
// SOURCE: cursive-bootstrap/src/03_analysis/types/type_predicates.cpp
//
// =============================================================================

#include "04_analysis/typing/type_predicates.h"

#include <algorithm>
#include <set>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_decls.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_lookup.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/resolve/scopes.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsTypePredicates() {
  SPEC_DEF("BitcopyType", "9.2");
  SPEC_DEF("CloneType", "9.2");
  SPEC_DEF("DropType", "9.2");
  SPEC_DEF("FfiSafeType", "9.2");
  SPEC_DEF("GpuSafeType", "20.2.4");
  SPEC_DEF("GpuSafePrimTypes", "20.2.4");
  SPEC_DEF("ProhibitedGpuType", "20.2.4");
  SPEC_DEF("GpuSafeComponents", "20.2.4");
  SPEC_DEF("HasGpuSafeReq", "20.2.4");
  SPEC_DEF("GpuSafePredicateClauseOk", "20.2.4");
  SPEC_DEF("GpuSafe-Prim", "20.2.4");
  SPEC_DEF("GpuSafe-RawPtr", "20.2.4");
  SPEC_DEF("GpuSafe-Array", "20.2.4");
  SPEC_DEF("GpuSafe-Tuple", "20.2.4");
  SPEC_DEF("GpuSafe-Perm", "20.2.4");
  SPEC_DEF("GpuSafe-Record", "20.2.4");
  SPEC_DEF("GpuSafe-Enum", "20.2.4");
  SPEC_DEF("GpuSafe-StringView", "20.2.4");
  SPEC_DEF("GpuSafe-BytesView", "20.2.4");
  SPEC_DEF("GpuSafeType-Err", "20.2.4");
  SPEC_DEF("GpuSafe-Record-Field-Err", "20.2.4");
  SPEC_DEF("GpuSafe-Generic-Unbounded-Err", "20.2.4");
  SPEC_DEF("ZeroableType", "23.3.4");
  SPEC_DEF("BuiltinStepType", "14.10.4");
}

TypeRef NormalizeFoundationalBuiltinBase(const TypeRef& type) {
  if (!type) {
    return nullptr;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return NormalizeFoundationalBuiltinBase(node.base);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return NormalizeFoundationalBuiltinBase(node.base);
        } else {
          return type;
        }
      },
      type->node);
}

TypeRef ConstType(const TypeRef& base) {
  return MakeTypePerm(Permission::Const, base);
}

static bool HasLayoutC(const ast::AttributeList& attrs) {
  for (const auto& attr : attrs) {
    if (attr.name != "layout" || attr.args.empty()) {
      continue;
    }
    const auto* token = std::get_if<ast::Token>(&attr.args[0].value);
    if (!token) {
      continue;
    }
    if (token->lexeme == "C") {
      return true;
    }
  }
  return false;
}

static bool AstTypeMentionsParam(const std::shared_ptr<ast::Type>& type,
                                 std::string_view param_name) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          if (node.path.size() == 1 && IdEq(node.path[0], param_name)) {
            return true;
          }
          for (const auto& arg : node.generic_args) {
            if (AstTypeMentionsParam(arg, param_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeApply>) {
          if (node.path.size() == 1 && IdEq(node.path[0], param_name)) {
            return true;
          }
          for (const auto& arg : node.args) {
            if (AstTypeMentionsParam(arg, param_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (AstTypeMentionsParam(elem, param_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return AstTypeMentionsParam(node.element, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return AstTypeMentionsParam(node.element, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return AstTypeMentionsParam(node.element, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return AstTypeMentionsParam(node.element, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& member : node.types) {
            if (AstTypeMentionsParam(member, param_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            if (AstTypeMentionsParam(param.type, param_name)) {
              return true;
            }
          }
          return AstTypeMentionsParam(node.ret, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          for (const auto& param : node.params) {
            if (AstTypeMentionsParam(param.type, param_name)) {
              return true;
            }
          }
          if (AstTypeMentionsParam(node.ret, param_name)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (AstTypeMentionsParam(dep.type, param_name)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          for (const auto& arg : node.generic_args) {
            if (AstTypeMentionsParam(arg, param_name)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRange>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRangeInclusive>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRangeFrom>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRangeTo>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else if constexpr (std::is_same_v<T, ast::TypeRangeToInclusive>) {
          return AstTypeMentionsParam(node.base, param_name);
        } else {
          return false;
        }
      },
      type->node);
}

static bool HasFfiSafeReq(const std::optional<ast::PredicateClause>& where_clause_opt,
                          std::string_view param_name) {
  if (!where_clause_opt.has_value()) {
    return false;
  }
  for (const auto& pred : *where_clause_opt) {
    if (!IdEq(pred.pred, "FfiSafe") || !pred.type) {
      continue;
    }
    const auto* path = std::get_if<ast::TypePathType>(&pred.type->node);
    if (!path || !path->generic_args.empty() || path->path.size() != 1) {
      continue;
    }
    if (IdEq(path->path[0], param_name)) {
      return true;
    }
  }
  return false;
}

static bool HasGpuSafeReq(const std::optional<ast::PredicateClause>& where_clause_opt,
                         std::string_view param_name) {
  SPEC_RULE("HasGpuSafeReq");
  if (!where_clause_opt.has_value()) {
    return false;
  }
  for (const auto& pred : *where_clause_opt) {
    if (!IdEq(pred.pred, "GpuSafe") || !pred.type) {
      continue;
    }
    const auto* path = std::get_if<ast::TypePathType>(&pred.type->node);
    if (!path || path->path.size() != 1) {
      continue;
    }
    if (IdEq(path->path[0], param_name)) {
      return true;
    }
  }
  return false;
}

static std::vector<std::string> FfiSafeRecordTypeParamsInFields(
    const ast::RecordDecl& decl) {
  std::vector<std::string> used;
  if (!decl.generic_params.has_value()) {
    return used;
  }
  for (const auto& param : decl.generic_params->params) {
    bool mentioned = false;
    for (const auto& member : decl.members) {
      const auto* field = std::get_if<ast::FieldDecl>(&member);
      if (!field || !field->type) {
        continue;
      }
      if (AstTypeMentionsParam(field->type, param.name)) {
        mentioned = true;
        break;
      }
    }
    if (mentioned) {
      used.push_back(param.name);
    }
  }
  return used;
}

static std::vector<std::string> GpuSafeRecordTypeParamsInFields(
    const ast::RecordDecl& decl) {
  std::vector<std::string> used;
  if (!decl.generic_params.has_value()) {
    return used;
  }
  for (const auto& param : decl.generic_params->params) {
    bool mentioned = false;
    for (const auto& member : decl.members) {
      const auto* field = std::get_if<ast::FieldDecl>(&member);
      if (!field || !field->type) {
        continue;
      }
      if (AstTypeMentionsParam(field->type, param.name)) {
        mentioned = true;
        break;
      }
    }
    if (mentioned) {
      used.push_back(param.name);
    }
  }
  return used;
}

static std::vector<std::string> FfiSafeEnumTypeParamsInPayloads(
    const ast::EnumDecl& decl) {
  std::vector<std::string> used;
  if (!decl.generic_params.has_value()) {
    return used;
  }
  for (const auto& param : decl.generic_params->params) {
    bool mentioned = false;
    for (const auto& variant : decl.variants) {
      if (!variant.payload_opt.has_value()) {
        continue;
      }
      std::visit(
          [&](const auto& payload) {
            using P = std::decay_t<decltype(payload)>;
            if (mentioned) {
              return;
            }
            if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
              for (const auto& elem : payload.elements) {
                if (AstTypeMentionsParam(elem, param.name)) {
                  mentioned = true;
                  return;
                }
              }
            } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
              for (const auto& field : payload.fields) {
                if (AstTypeMentionsParam(field.type, param.name)) {
                  mentioned = true;
                  return;
                }
              }
            }
          },
          *variant.payload_opt);
      if (mentioned) {
        break;
      }
    }
    if (mentioned) {
      used.push_back(param.name);
    }
  }
  return used;
}

static std::vector<std::string> GpuSafeEnumTypeParamsInPayloads(
    const ast::EnumDecl& decl) {
  std::vector<std::string> used;
  if (!decl.generic_params.has_value()) {
    return used;
  }
  for (const auto& param : decl.generic_params->params) {
    bool mentioned = false;
    for (const auto& variant : decl.variants) {
      if (!variant.payload_opt.has_value()) {
        continue;
      }
      std::visit(
          [&](const auto& payload) {
            using P = std::decay_t<decltype(payload)>;
            if (mentioned) {
              return;
            }
            if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
              for (const auto& elem : payload.elements) {
                if (AstTypeMentionsParam(elem, param.name)) {
                  mentioned = true;
                  return;
                }
              }
            } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
              for (const auto& field : payload.fields) {
                if (AstTypeMentionsParam(field.type, param.name)) {
                  mentioned = true;
                  return;
                }
              }
            }
          },
          *variant.payload_opt);
      if (mentioned) {
        break;
      }
    }
    if (mentioned) {
      used.push_back(param.name);
    }
  }
  return used;
}

static bool GenericParamsMissingFfiSafeReqs(
    const std::optional<ast::GenericParams>& generic_params_opt,
    const std::optional<ast::PredicateClause>& where_clause_opt,
    const std::vector<std::string>& used_params) {
  if (!generic_params_opt.has_value() || used_params.empty()) {
    return false;
  }
  for (const auto& name : used_params) {
    if (!HasFfiSafeReq(where_clause_opt, name)) {
      return true;
    }
  }
  return false;
}

static bool GenericParamsMissingGpuSafeReqs(
    const std::optional<ast::GenericParams>& generic_params_opt,
    const std::optional<ast::PredicateClause>& where_clause_opt,
    const std::vector<std::string>& used_params) {
  SPEC_RULE("GpuSafePredicateClauseOk");
  if (!generic_params_opt.has_value() || used_params.empty()) {
    return false;
  }
  for (const auto& name : used_params) {
    if (!HasGpuSafeReq(where_clause_opt, name)) {
      return true;
    }
  }
  return false;
}

static ast::Path AsAstPath(const TypePath& path) {
  ast::Path out;
  out.reserve(path.size());
  for (const auto& seg : path) {
    out.push_back(seg);
  }
  return out;
}

static const TypeDecl* ResolveNominalTypeDeclForFfi(
    const ScopeContext& ctx,
    const ast::ModulePath* current_module,
    const TypePath& path,
    ast::Path* resolved_path) {
  if (path.empty()) {
    return nullptr;
  }

  const auto exact = AsAstPath(path);
  auto exact_it = ctx.sigma.types.find(PathKeyOf(exact));
  if (exact_it != ctx.sigma.types.end()) {
    if (resolved_path) {
      *resolved_path = exact;
    }
    return &exact_it->second;
  }

  if (current_module && !current_module->empty()) {
    ast::Path current_qualified = *current_module;
    current_qualified.insert(current_qualified.end(), path.begin(), path.end());
    auto current_it = ctx.sigma.types.find(PathKeyOf(current_qualified));
    if (current_it != ctx.sigma.types.end()) {
      if (resolved_path) {
        *resolved_path = current_qualified;
      }
      return &current_it->second;
    }

    if (path.size() == 1) {
      ast::Path root_qualified;
      root_qualified.push_back(current_module->front());
      root_qualified.push_back(path.front());
      auto root_it = ctx.sigma.types.find(PathKeyOf(root_qualified));
      if (root_it != ctx.sigma.types.end()) {
        if (resolved_path) {
          *resolved_path = root_qualified;
        }
        return &root_it->second;
      }
    }
  }

  std::optional<ast::Path> unique_match;
  for (const auto& [key, _decl] : ctx.sigma.types) {
    if (key.empty() || key.size() < path.size()) {
      continue;
    }
    bool suffix_match = true;
    for (std::size_t i = 0; i < path.size(); ++i) {
      const auto key_index = key.size() - path.size() + i;
      if (!IdEq(key[key_index], path[i])) {
        suffix_match = false;
        break;
      }
    }
    if (!suffix_match) {
      continue;
    }

    ast::Path candidate(key.begin(), key.end());
    if (unique_match.has_value()) {
      return nullptr;
    }
    unique_match = std::move(candidate);
  }

  if (!unique_match.has_value()) {
    return nullptr;
  }
  auto match_it = ctx.sigma.types.find(PathKeyOf(*unique_match));
  if (match_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  if (resolved_path) {
    *resolved_path = *unique_match;
  }
  return &match_it->second;
}

static TypeRef StripPermAndRefine(const TypeRef& type) {
  TypeRef cur = type;
  while (cur) {
    if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
      cur = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
      cur = refine->base;
      continue;
    }
    break;
  }
  return cur;
}

static bool IsIntPrim(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "i128" || name == "isize" || name == "u8" ||
         name == "u16" || name == "u32" || name == "u64" ||
         name == "u128" || name == "usize";
}

static bool IsFloatPrim(std::string_view name) {
  return name == "f16" || name == "f32" || name == "f64";
}

static bool IsNumericPrim(std::string_view name) {
  return IsIntPrim(name) || IsFloatPrim(name);
}

static bool IsBuiltinBitcopyPath(const TypePath& path) {
  return path.size() == 1 &&
         (IdEq(path[0], "FileKind") ||
          IdEq(path[0], "IoError") ||
          IdEq(path[0], "TimeError") ||
          IdEq(path[0], "Duration") ||
          IdEq(path[0], "MonotonicInstant") ||
          IdEq(path[0], "UtcInstant") ||
          IdEq(path[0], "Context") ||
          IdEq(path[0], "System"));
}

static bool IsGpuSafePrim(std::string_view name) {
  SPEC_RULE("GpuSafePrimTypes");
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
         name == "isize" || name == "usize" || name == "f16" ||
         name == "f32" || name == "f64" || name == "bool" || name == "()";
}

static std::vector<std::string> GenericParamNames(
    const std::optional<ast::GenericParams>& generic_params_opt) {
  std::vector<std::string> names;
  if (!generic_params_opt.has_value()) {
    return names;
  }
  names.reserve(generic_params_opt->params.size());
  for (const auto& param : generic_params_opt->params) {
    names.push_back(param.name);
  }
  return names;
}

static std::optional<std::vector<TypeRef>> ResolveDeclGenericArgs(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& generic_params_opt,
    const std::vector<TypeRef>& provided_args) {
  if (!generic_params_opt.has_value()) {
    if (!provided_args.empty()) {
      return std::nullopt;
    }
    return std::vector<TypeRef>{};
  }

  const auto& params = generic_params_opt->params;
  if (provided_args.size() > params.size()) {
    return std::nullopt;
  }

  std::vector<TypeRef> out;
  out.reserve(params.size());
  for (std::size_t i = 0; i < params.size(); ++i) {
    if (i < provided_args.size()) {
      out.push_back(provided_args[i]);
      continue;
    }
    if (!params[i].default_type) {
      return std::nullopt;
    }
    const auto lowered = LowerType(ctx, params[i].default_type);
    if (!lowered.ok) {
      return std::nullopt;
    }
    out.push_back(lowered.type);
  }

  return out;
}

static TypeRef ApplyDeclGenericArgs(const TypeRef& type,
                                    const std::vector<std::string>& param_names,
                                    const std::vector<TypeRef>& args) {
  if (!type) {
    return type;
  }
  if (param_names.empty()) {
    return type;
  }
  return ApplyGenericSubstitution(type, param_names, args);
}

static bool TypeEq(const TypeRef& lhs, const TypeRef& rhs) {
  const auto eq = TypeEquiv(lhs, rhs);
  return eq.ok && eq.equiv;
}

static bool IsUnitType(const TypeRef& type) {
  const auto stripped = StripPermAndRefine(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  return prim && prim->name == "()";
}

static bool HasCloneMethod(const ScopeContext& ctx, const TypeRef& stripped_type) {
    const auto* path = stripped_type ? AppliedTypePath(*stripped_type) : nullptr;
    if (!path) {
      return false;
    }
    const auto* record = LookupRecordDecl(ctx, *path);
  if (!record) {
    return false;
  }

  const auto expected_self = MakeTypePerm(Permission::Const, stripped_type);
  for (const auto& member : record->members) {
    const auto* method = std::get_if<ast::MethodDecl>(&member);
    if (!method || !IdEq(method->name, "clone") || !method->params.empty()) {
      continue;
    }
    const auto sig = BuildMethodSignature(
        ctx, stripped_type, method->receiver, method->params, method->return_type_opt);
    if (!sig.ok || !sig.func_type) {
      continue;
    }
    const auto* fn = std::get_if<TypeFunc>(&sig.func_type->node);
    if (!fn || fn->params.size() != 1) {
      continue;
    }
    if (fn->params[0].mode.has_value()) {
      continue;
    }
    if (!TypeEq(fn->params[0].type, expected_self)) {
      continue;
    }
    if (!TypeEq(sig.return_type, stripped_type)) {
      continue;
    }
    return true;
  }
  return false;
}

static bool HasDropMethod(const ScopeContext& ctx, const TypeRef& stripped_type) {
    const auto* path = stripped_type ? AppliedTypePath(*stripped_type) : nullptr;
    if (!path) {
      return false;
    }
    const auto* record = LookupRecordDecl(ctx, *path);
  if (!record) {
    return false;
  }

  const auto expected_self = MakeTypePerm(Permission::Unique, stripped_type);
  for (const auto& member : record->members) {
    const auto* method = std::get_if<ast::MethodDecl>(&member);
    if (!method || !IdEq(method->name, "drop") || !method->params.empty()) {
      continue;
    }
    const auto sig = BuildMethodSignature(
        ctx, stripped_type, method->receiver, method->params, method->return_type_opt);
    if (!sig.ok || !sig.func_type) {
      continue;
    }
    const auto* fn = std::get_if<TypeFunc>(&sig.func_type->node);
    if (!fn || fn->params.size() != 1) {
      continue;
    }
    if (fn->params[0].mode.has_value()) {
      continue;
    }
    if (!TypeEq(fn->params[0].type, expected_self)) {
      continue;
    }
    if (!IsUnitType(sig.return_type)) {
      continue;
    }
    return true;
  }
  return false;
}

static bool BitcopyTypeImpl(const ScopeContext& ctx,
                            const TypeRef& type,
                            std::set<PathKey>& active_paths);

static bool CheckRecordBitcopy(const ScopeContext& ctx,
                               const ast::RecordDecl& decl,
                               const std::vector<TypeRef>& args,
                               std::set<PathKey>& active_paths) {
  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& member : decl.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field || !field->type) {
      continue;
    }
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok) {
      return false;
    }
    const auto field_type = ApplyDeclGenericArgs(lowered.type, param_names, args);
    if (!BitcopyTypeImpl(ctx, field_type, active_paths)) {
      return false;
    }
  }
  return true;
}

static bool CheckEnumBitcopy(const ScopeContext& ctx,
                             const ast::EnumDecl& decl,
                             const std::vector<TypeRef>& args,
                             std::set<PathKey>& active_paths) {
  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& variant : decl.variants) {
    if (!variant.payload_opt.has_value()) {
      continue;
    }
    bool payload_ok = true;
    std::visit(
        [&](const auto& payload) {
          using P = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
            for (const auto& elem : payload.elements) {
              const auto lowered = LowerType(ctx, elem);
              if (!lowered.ok) {
                payload_ok = false;
                return;
              }
              const auto elem_type =
                  ApplyDeclGenericArgs(lowered.type, param_names, args);
              if (!BitcopyTypeImpl(ctx, elem_type, active_paths)) {
                payload_ok = false;
                return;
              }
            }
          } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
            for (const auto& field : payload.fields) {
              const auto lowered = LowerType(ctx, field.type);
              if (!lowered.ok) {
                payload_ok = false;
                return;
              }
              const auto field_type =
                  ApplyDeclGenericArgs(lowered.type, param_names, args);
              if (!BitcopyTypeImpl(ctx, field_type, active_paths)) {
                payload_ok = false;
                return;
              }
            }
          }
        },
        *variant.payload_opt);
    if (!payload_ok) {
      return false;
    }
  }
  return true;
}

static bool CheckModalBitcopy(const ScopeContext& ctx,
                              const ast::ModalDecl& decl,
                              const std::vector<TypeRef>& args,
                              const std::optional<std::string_view>& state_filter,
                              std::set<PathKey>& active_paths) {
  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& state : decl.states) {
    if (state_filter.has_value() && !IdEq(state.name, *state_filter)) {
      continue;
    }
    for (const auto& member : state.members) {
      const auto* field = std::get_if<ast::StateFieldDecl>(&member);
      if (!field || !field->type) {
        continue;
      }
      const auto lowered = LowerType(ctx, field->type);
      if (!lowered.ok) {
        return false;
      }
      const auto field_type = ApplyDeclGenericArgs(lowered.type, param_names, args);
      if (!BitcopyTypeImpl(ctx, field_type, active_paths)) {
        return false;
      }
    }
  }
  return true;
}

static bool BitcopyTypeImpl(const ScopeContext& ctx,
                            const TypeRef& type,
                            std::set<PathKey>& active_paths) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          if (node.perm == Permission::Unique) {
            return false;
          }
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          return std::all_of(node.elements.begin(), node.elements.end(),
                             [&](const TypeRef& elem) {
                               return BitcopyTypeImpl(ctx, elem, active_paths);
                             });
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return BitcopyTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          return std::all_of(node.members.begin(), node.members.end(),
                             [&](const TypeRef& member) {
                               return BitcopyTypeImpl(ctx, member, active_paths);
                             });
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          return BitcopyTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return node.state.has_value() && node.state == StringState::View;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return node.state.has_value() && node.state == BytesState::View;
        } else if constexpr (std::is_same_v<T, TypePathType> ||
                             std::is_same_v<T, TypeApply>) {
          const TypePath& applied_path = node.path;
          const std::vector<TypeRef>& applied_args =
              [&]() -> const std::vector<TypeRef>& {
            if constexpr (std::is_same_v<T, TypePathType>) {
              return node.generic_args;
            } else {
              return node.args;
            }
          }();

          if (IsBuiltinBitcopyPath(applied_path)) {
            return true;
          }
          ast::Path ast_path(applied_path.begin(), applied_path.end());
          const auto key = PathKeyOf(ast_path);
          if (active_paths.find(key) != active_paths.end()) {
            return false;
          }
          const auto it = ctx.sigma.types.find(key);
          if (it == ctx.sigma.types.end()) {
            return false;
          }

          active_paths.insert(key);
          const bool ok = std::visit(
              [&](const auto& decl) -> bool {
                using D = std::decay_t<decltype(decl)>;
                if constexpr (std::is_same_v<D, ast::RecordDecl>) {
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, applied_args);
                  return args.has_value() &&
                         CheckRecordBitcopy(ctx, decl, *args, active_paths);
                } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, applied_args);
                  return args.has_value() &&
                         CheckEnumBitcopy(ctx, decl, *args, active_paths);
                } else if constexpr (std::is_same_v<D, ast::ModalDecl>) {
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, applied_args);
                  return args.has_value() &&
                         CheckModalBitcopy(ctx, decl, *args, std::nullopt, active_paths);
                } else if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
                  if (!decl.type) {
                    return false;
                  }
                  const auto lowered = LowerType(ctx, decl.type);
                  if (!lowered.ok) {
                    return false;
                  }
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, applied_args);
                  if (!args.has_value()) {
                    return false;
                  }
                  const auto param_names = GenericParamNames(decl.generic_params);
                  const auto alias_target =
                      ApplyDeclGenericArgs(lowered.type, param_names, *args);
                  return BitcopyTypeImpl(ctx, alias_target, active_paths);
                } else {
                  return false;
                }
              },
              it->second);
          active_paths.erase(key);
          return ok;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          const auto* decl = LookupModalDecl(ctx, node.path);
          if (!decl) {
            return false;
          }
          ast::Path ast_path(node.path.begin(), node.path.end());
          const auto key = PathKeyOf(ast_path);
          if (active_paths.find(key) != active_paths.end()) {
            return false;
          }
          const auto args = ResolveDeclGenericArgs(
              ctx, decl->generic_params, node.generic_args);
          if (!args.has_value()) {
            return false;
          }
          active_paths.insert(key);
          const bool ok = CheckModalBitcopy(ctx, *decl, *args, node.state, active_paths);
          active_paths.erase(key);
          return ok;
        } else {
          return false;
        }
      },
      type->node);
}

static bool IsFfiSafePrim(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "i128" || name == "isize" || name == "u8" ||
         name == "u16" || name == "u32" || name == "u64" ||
         name == "u128" || name == "usize" || name == "f16" ||
         name == "f32" || name == "f64" || name == "char" || name == "()";
}

static ast::ModulePath ModulePathOfResolvedPath(const ast::Path& path) {
  ast::ModulePath out;
  if (path.empty()) {
    return out;
  }
  out.assign(path.begin(), path.end() - 1);
  return out;
}

static std::optional<std::string_view> FfiSafeDiagForTypeImpl(
    const ScopeContext& ctx,
    const ast::ModulePath* current_module,
    const TypeRef& type,
    std::set<PathKey>& active_paths);

static std::optional<std::string_view> FfiSafeRecordDiag(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const std::vector<TypeRef>& generic_args,
    const ast::ModulePath& decl_module,
    std::set<PathKey>& active_paths) {
  if (!HasLayoutC(decl.attrs)) {
    return std::optional<std::string_view>{"E-TYP-2624"};
  }
  if (GenericParamsMissingFfiSafeReqs(decl.generic_params,
                                      decl.predicate_clause_opt,
                                      FfiSafeRecordTypeParamsInFields(decl))) {
    return std::optional<std::string_view>{"E-TYP-2629"};
  }

  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& member : decl.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field || !field->type) {
      continue;
    }
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok) {
      return std::optional<std::string_view>{"E-TYP-2628"};
    }
    const auto instantiated =
        ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
    const auto field_diag =
        FfiSafeDiagForTypeImpl(ctx, &decl_module, instantiated, active_paths);
    if (field_diag.has_value()) {
      if (*field_diag == "E-TYP-2628") {
        return field_diag;
      }
      return std::optional<std::string_view>{"E-TYP-2626"};
    }
  }
  return std::nullopt;
}

static std::optional<std::string_view> FfiSafeEnumDiag(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const std::vector<TypeRef>& generic_args,
    const ast::ModulePath& decl_module,
    std::set<PathKey>& active_paths) {
  if (!HasLayoutC(decl.attrs)) {
    return std::optional<std::string_view>{"E-TYP-2625"};
  }
  if (GenericParamsMissingFfiSafeReqs(decl.generic_params,
                                      decl.predicate_clause_opt,
                                      FfiSafeEnumTypeParamsInPayloads(decl))) {
    return std::optional<std::string_view>{"E-TYP-2629"};
  }

  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& variant : decl.variants) {
    if (!variant.payload_opt.has_value()) {
      continue;
    }
    std::optional<std::string_view> bad_payload = std::nullopt;
    std::visit(
        [&](const auto& payload) {
          using P = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
            for (const auto& elem : payload.elements) {
              const auto lowered = LowerType(ctx, elem);
              if (!lowered.ok) {
                bad_payload = "E-TYP-2628";
                return;
              }
              const auto instantiated =
                  ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
              const auto elem_diag =
                  FfiSafeDiagForTypeImpl(ctx, &decl_module, instantiated,
                                         active_paths);
              if (elem_diag.has_value()) {
                if (*elem_diag == "E-TYP-2628") {
                  bad_payload = "E-TYP-2628";
                  return;
                }
                bad_payload = "E-TYP-2627";
                return;
              }
            }
          } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
            for (const auto& field : payload.fields) {
              const auto lowered = LowerType(ctx, field.type);
              if (!lowered.ok) {
                bad_payload = "E-TYP-2628";
                return;
              }
              const auto instantiated =
                  ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
              const auto field_diag =
                  FfiSafeDiagForTypeImpl(ctx, &decl_module, instantiated,
                                         active_paths);
              if (field_diag.has_value()) {
                if (*field_diag == "E-TYP-2628") {
                  bad_payload = "E-TYP-2628";
                  return;
                }
                bad_payload = "E-TYP-2627";
                return;
              }
            }
          }
        },
        *variant.payload_opt);
    if (bad_payload.has_value()) {
      return bad_payload;
    }
  }
  return std::nullopt;
}

static std::optional<std::string_view> FfiSafeDiagForTypeImpl(
    const ScopeContext& ctx,
    const ast::ModulePath* current_module,
    const TypeRef& type,
    std::set<PathKey>& active_paths) {
  if (!type) {
    return std::optional<std::string_view>{"E-TYP-2628"};
  }

  return std::visit(
      [&](const auto& node) -> std::optional<std::string_view> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          if (IsFfiSafePrim(node.name)) {
            return std::nullopt;
          }
          return std::optional<std::string_view>{"E-TYP-2623"};
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return FfiSafeDiagForTypeImpl(ctx, current_module, node.base,
                                        active_paths);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return FfiSafeDiagForTypeImpl(ctx, current_module, node.base,
                                        active_paths);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return FfiSafeDiagForTypeImpl(ctx, current_module, node.element,
                                        active_paths);
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            const auto diag = FfiSafeDiagForTypeImpl(ctx, current_module,
                                                     param.type, active_paths);
            if (diag.has_value()) {
              return diag;
            }
          }
          return FfiSafeDiagForTypeImpl(ctx, current_module, node.ret,
                                        active_paths);
        } else if constexpr (std::is_same_v<T, TypePathType> ||
                             std::is_same_v<T, TypeApply>) {
          const TypePath& applied_path = node.path;
          const std::vector<TypeRef>& applied_args =
              [&]() -> const std::vector<TypeRef>& {
            if constexpr (std::is_same_v<T, TypePathType>) {
              return node.generic_args;
            } else {
              return node.args;
            }
          }();

          if (applied_path.size() == 1 && IdEq(applied_path[0], "Context")) {
            return std::optional<std::string_view>{"E-TYP-2623"};
          }

          ast::Path resolved_path;
          const TypeDecl* decl =
              ResolveNominalTypeDeclForFfi(ctx, current_module, applied_path,
                                           &resolved_path);
          if (!decl) {
            return std::optional<std::string_view>{"E-TYP-2628"};
          }

          const auto key = PathKeyOf(resolved_path);
          if (active_paths.find(key) != active_paths.end()) {
            return std::optional<std::string_view>{"E-TYP-2628"};
          }

          active_paths.insert(key);
          const auto decl_module = ModulePathOfResolvedPath(resolved_path);
          const auto diag = std::visit(
              [&](const auto& resolved_decl) -> std::optional<std::string_view> {
                using D = std::decay_t<decltype(resolved_decl)>;
                if constexpr (std::is_same_v<D, ast::RecordDecl>) {
                  if (applied_args.empty() &&
                      GenericParamsMissingFfiSafeReqs(
                          resolved_decl.generic_params,
                          resolved_decl.predicate_clause_opt,
                          FfiSafeRecordTypeParamsInFields(resolved_decl))) {
                    return std::optional<std::string_view>{"E-TYP-2629"};
                  }
                  const auto args = ResolveDeclGenericArgs(
                      ctx, resolved_decl.generic_params, applied_args);
                  if (!args.has_value()) {
                    return std::optional<std::string_view>{"E-TYP-2628"};
                  }
                  return FfiSafeRecordDiag(ctx, resolved_decl, *args, decl_module,
                                           active_paths);
                } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
                  if (applied_args.empty() &&
                      GenericParamsMissingFfiSafeReqs(
                          resolved_decl.generic_params,
                          resolved_decl.predicate_clause_opt,
                          FfiSafeEnumTypeParamsInPayloads(resolved_decl))) {
                    return std::optional<std::string_view>{"E-TYP-2629"};
                  }
                  const auto args = ResolveDeclGenericArgs(
                      ctx, resolved_decl.generic_params, applied_args);
                  if (!args.has_value()) {
                    return std::optional<std::string_view>{"E-TYP-2628"};
                  }
                  return FfiSafeEnumDiag(ctx, resolved_decl, *args, decl_module,
                                         active_paths);
                } else if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
                  if (!resolved_decl.type) {
                    return std::optional<std::string_view>{"E-TYP-2628"};
                  }
                  const auto args = ResolveDeclGenericArgs(
                      ctx, resolved_decl.generic_params, applied_args);
                  if (!args.has_value()) {
                    return std::optional<std::string_view>{"E-TYP-2628"};
                  }
                  const auto lowered = LowerType(ctx, resolved_decl.type);
                  if (!lowered.ok) {
                    return std::optional<std::string_view>{"E-TYP-2628"};
                  }
                  const auto param_names =
                      GenericParamNames(resolved_decl.generic_params);
                  const auto target =
                      ApplyDeclGenericArgs(lowered.type, param_names, *args);
                  return FfiSafeDiagForTypeImpl(ctx, &decl_module, target,
                                                active_paths);
                } else {
                  return std::optional<std::string_view>{"E-TYP-2623"};
                }
              },
              *decl);
          active_paths.erase(key);
          return diag;
        } else if constexpr (std::is_same_v<T, TypeClosure> ||
                             std::is_same_v<T, TypeDynamic> ||
                             std::is_same_v<T, TypeModalState> ||
                             std::is_same_v<T, TypeOpaque> ||
                             std::is_same_v<T, TypeTuple> ||
                             std::is_same_v<T, TypeUnion> ||
                             std::is_same_v<T, TypeSlice> ||
                             std::is_same_v<T, TypeString> ||
                             std::is_same_v<T, TypeBytes> ||
                             std::is_same_v<T, TypePtr> ||
                             std::is_same_v<T, TypeRange> ||
                             std::is_same_v<T, TypeRangeInclusive> ||
                             std::is_same_v<T, TypeRangeFrom> ||
                             std::is_same_v<T, TypeRangeTo> ||
                             std::is_same_v<T, TypeRangeToInclusive> ||
                             std::is_same_v<T, TypeRangeFull>) {
          return std::optional<std::string_view>{"E-TYP-2623"};
        } else {
          return std::optional<std::string_view>{"E-TYP-2623"};
        }
      },
      type->node);
}

static bool FfiSafeTypeImpl(const ScopeContext& ctx,
                            const ast::ModulePath* current_module,
                            const TypeRef& type,
                            std::set<PathKey>& active_paths) {
  return !FfiSafeDiagForTypeImpl(ctx, current_module, type, active_paths)
              .has_value();
}

static bool ZeroableTypeImpl(const ScopeContext& ctx,
                             const TypeRef& type,
                             std::set<PathKey>& active_paths);

static bool ZeroableDeclType(const ScopeContext& ctx,
                             const std::shared_ptr<ast::Type>& syntax_type,
                             const std::vector<std::string>& param_names,
                             const std::vector<TypeRef>& args,
                             std::set<PathKey>& active_paths) {
  if (!syntax_type) {
    return false;
  }
  const auto lowered = LowerType(ctx, syntax_type);
  if (!lowered.ok) {
    return false;
  }
  const auto instantiated =
      ApplyDeclGenericArgs(lowered.type, param_names, args);
  return instantiated &&
         ZeroableTypeImpl(ctx, instantiated, active_paths);
}

static bool ZeroableEnumPayload(const ScopeContext& ctx,
                                const ast::VariantDecl& variant,
                                const std::vector<std::string>& param_names,
                                const std::vector<TypeRef>& args,
                                std::set<PathKey>& active_paths) {
  if (!variant.payload_opt.has_value()) {
    return true;
  }

  return std::visit(
      [&](const auto& payload) -> bool {
        using P = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
          for (const auto& elem : payload.elements) {
            if (!ZeroableDeclType(ctx, elem, param_names, args, active_paths)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
          for (const auto& field : payload.fields) {
            if (!ZeroableDeclType(
                    ctx, field.type, param_names, args, active_paths)) {
              return false;
            }
          }
          return true;
        } else {
          return false;
        }
      },
      *variant.payload_opt);
}

static bool ZeroableTypeImpl(const ScopeContext& ctx,
                             const TypeRef& type,
                             std::set<PathKey>& active_paths) {
  const auto stripped = StripPermAndRefine(type);
  if (!stripped) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return node.name != "!";
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (!ZeroableTypeImpl(ctx, elem, active_paths)) {
              return false;
            }
          }
          return true;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return ZeroableTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return node.state.has_value() && *node.state != PtrState::Valid;
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          const auto key = PathKeyOf(ast::Path(node.path.begin(), node.path.end()));
          const auto it = ctx.sigma.types.find(key);
          if (it == ctx.sigma.types.end()) {
            return false;
          }
          if (active_paths.find(key) != active_paths.end()) {
            return false;
          }
          active_paths.insert(key);
          const bool zeroable = std::visit(
              [&](const auto& decl) -> bool {
                using D = std::decay_t<decltype(decl)>;
                if constexpr (std::is_same_v<D, ast::RecordDecl>) {
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, node.generic_args);
                  if (!args.has_value()) {
                    return false;
                  }
                  const auto param_names = GenericParamNames(decl.generic_params);
                  for (const auto& member : decl.members) {
                    const auto* field = std::get_if<ast::FieldDecl>(&member);
                    if (!field || !field->type) {
                      continue;
                    }
                    if (!ZeroableDeclType(
                            ctx, field->type, param_names, *args, active_paths)) {
                      return false;
                    }
                  }
                  return true;
                } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, node.generic_args);
                  if (!args.has_value()) {
                    return false;
                  }
                  const auto discs = EnumDiscriminants(decl);
                  if (!discs.ok || discs.discs.size() != decl.variants.size()) {
                    return false;
                  }
                  const auto param_names = GenericParamNames(decl.generic_params);
                  for (std::size_t i = 0; i < decl.variants.size(); ++i) {
                    if (discs.discs[i] != 0) {
                      continue;
                    }
                    if (ZeroableEnumPayload(
                            ctx,
                            decl.variants[i],
                            param_names,
                            *args,
                            active_paths)) {
                      return true;
                    }
                  }
                  return false;
                } else if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
                  if (!decl.type) {
                    return false;
                  }
                  const auto lowered = LowerType(ctx, decl.type);
                  if (!lowered.ok) {
                    return false;
                  }
                  const auto args = ResolveDeclGenericArgs(
                      ctx, decl.generic_params, node.generic_args);
                  if (!args.has_value()) {
                    return false;
                  }
                  const auto param_names = GenericParamNames(decl.generic_params);
                  const auto target =
                      ApplyDeclGenericArgs(lowered.type, param_names, *args);
                  return target &&
                         ZeroableTypeImpl(ctx, target, active_paths);
                } else {
                  return false;
                }
              },
              it->second);
          active_paths.erase(key);
          return zeroable;
        } else {
          return false;
        }
      },
      stripped->node);
}

}  // namespace

// =============================================================================
// EXPORTED: BitcopyType
// =============================================================================

bool BitcopyType(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("BitcopyType");
  std::set<PathKey> active_paths;
  return BitcopyTypeImpl(ctx, type, active_paths);
}

// =============================================================================
// EXPORTED: CloneType
// =============================================================================

bool CloneType(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("CloneType");

  if (!type) {
    return false;
  }

  if (BitcopyType(ctx, type)) {
    return true;
  }

  const auto stripped = StripPermAndRefine(type);
  return HasCloneMethod(ctx, stripped);
}

// =============================================================================
// EXPORTED: DropType
// =============================================================================

bool DropType(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("DropType");

  if (!type) {
    return false;
  }

  const auto stripped = StripPermAndRefine(type);
  if (!stripped) {
    return false;
  }
  if (const auto* str = std::get_if<TypeString>(&stripped->node)) {
    return str->state.has_value() && str->state == StringState::Managed;
  }
  if (const auto* bytes = std::get_if<TypeBytes>(&stripped->node)) {
    return bytes->state.has_value() && bytes->state == BytesState::Managed;
  }
  return HasDropMethod(ctx, stripped);
}

// =============================================================================
// EXPORTED: FfiSafeType
// =============================================================================

bool FfiSafeType(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("FfiSafeType");
  std::set<PathKey> active_paths;
  const ast::ModulePath* current_module =
      ctx.current_module.empty() ? nullptr : &ctx.current_module;
  return FfiSafeTypeImpl(ctx, current_module, type, active_paths);
}

std::optional<std::string_view> FfiSafeDiagForType(const ScopeContext& ctx,
                                                   const TypeRef& type) {
  std::set<PathKey> active_paths;
  const ast::ModulePath* current_module =
      ctx.current_module.empty() ? nullptr : &ctx.current_module;
  return FfiSafeDiagForTypeImpl(ctx, current_module, type, active_paths);
}

std::optional<std::string_view> FfiSafeDiagForType(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const TypeRef& type) {
  std::set<PathKey> active_paths;
  return FfiSafeDiagForTypeImpl(ctx, &current_module, type, active_paths);
}

// =============================================================================
// EXPORTED: GpuSafeType / GpuSafeDiagForType
// =============================================================================

static std::optional<std::string_view> GpuSafeDiagForTypeImpl(
    const ScopeContext& ctx,
    const TypeRef& type,
    std::set<PathKey>& active_paths);

static std::optional<std::string_view> GpuSafeRecordDiag(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const TypeRef& record_type,
    const std::vector<TypeRef>& generic_args,
    std::set<PathKey>& active_paths) {
  if (GenericParamsMissingGpuSafeReqs(decl.generic_params,
                                      decl.predicate_clause_opt,
                                      GpuSafeRecordTypeParamsInFields(decl))) {
    SPEC_RULE("GpuSafe-Generic-Unbounded-Err");
    return std::optional<std::string_view>{"E-TYP-2642"};
  }
  if (!BitcopyType(ctx, record_type)) {
    SPEC_RULE("GpuSafeType-Err");
    return std::optional<std::string_view>{"E-TYP-2640"};
  }

  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& member : decl.members) {
    const auto* field = std::get_if<ast::FieldDecl>(&member);
    if (!field || !field->type) {
      continue;
    }
    const auto lowered = LowerType(ctx, field->type);
    if (!lowered.ok) {
      SPEC_RULE("GpuSafe-Record-Field-Err");
      return std::optional<std::string_view>{"E-TYP-2640"};
    }
    const auto instantiated =
        ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
    if (GpuSafeDiagForTypeImpl(ctx, instantiated, active_paths).has_value()) {
      SPEC_RULE("GpuSafe-Record-Field-Err");
      return std::optional<std::string_view>{"E-TYP-2640"};
    }
  }
  SPEC_RULE("GpuSafe-Record");
  return std::nullopt;
}

static std::optional<std::string_view> GpuSafeEnumDiag(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const TypeRef& enum_type,
    const std::vector<TypeRef>& generic_args,
    std::set<PathKey>& active_paths) {
  if (GenericParamsMissingGpuSafeReqs(decl.generic_params,
                                      decl.predicate_clause_opt,
                                      GpuSafeEnumTypeParamsInPayloads(decl))) {
    SPEC_RULE("GpuSafe-Generic-Unbounded-Err");
    return std::optional<std::string_view>{"E-TYP-2642"};
  }
  if (!BitcopyType(ctx, enum_type)) {
    SPEC_RULE("GpuSafeType-Err");
    return std::optional<std::string_view>{"E-TYP-2640"};
  }

  const auto param_names = GenericParamNames(decl.generic_params);
  for (const auto& variant : decl.variants) {
    if (!variant.payload_opt.has_value()) {
      continue;
    }
    std::optional<std::string_view> bad_payload = std::nullopt;
    std::visit(
        [&](const auto& payload) {
          using P = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
            for (const auto& elem : payload.elements) {
              const auto lowered = LowerType(ctx, elem);
              if (!lowered.ok) {
                bad_payload = "E-TYP-2640";
                return;
              }
              const auto instantiated =
                  ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
              if (GpuSafeDiagForTypeImpl(ctx, instantiated, active_paths)
                      .has_value()) {
                bad_payload = "E-TYP-2640";
                return;
              }
            }
          } else if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
            for (const auto& field : payload.fields) {
              const auto lowered = LowerType(ctx, field.type);
              if (!lowered.ok) {
                bad_payload = "E-TYP-2640";
                return;
              }
              const auto instantiated =
                  ApplyDeclGenericArgs(lowered.type, param_names, generic_args);
              if (GpuSafeDiagForTypeImpl(ctx, instantiated, active_paths)
                      .has_value()) {
                bad_payload = "E-TYP-2640";
                return;
              }
            }
          }
        },
        *variant.payload_opt);
    if (bad_payload.has_value()) {
      SPEC_RULE("GpuSafeType-Err");
      return bad_payload;
    }
  }
  SPEC_RULE("GpuSafe-Enum");
  return std::nullopt;
}

static std::optional<std::string_view> GpuSafeDiagForTypeImpl(
    const ScopeContext& ctx,
    const TypeRef& type,
    std::set<PathKey>& active_paths) {
  if (!type) {
    return std::nullopt;
  }

  const auto stripped = StripPermAndRefine(type);
  if (!stripped) {
    return std::nullopt;
  }

  if (IsCapabilityType(stripped)) {
    SPEC_RULE("ProhibitedGpuType");
    SPEC_RULE("GpuSafeType-Err");
    return std::optional<std::string_view>{"E-TYP-2640"};
  }

  return std::visit(
      [&](const auto& node) -> std::optional<std::string_view> {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePerm>) {
          SPEC_RULE("GpuSafe-Perm");
          return GpuSafeDiagForTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return GpuSafeDiagForTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypePrim>) {
          if (IsGpuSafePrim(node.name)) {
            SPEC_RULE("GpuSafe-Prim");
            return std::nullopt;
          }
          SPEC_RULE("GpuSafeType-Err");
          return std::optional<std::string_view>{"E-TYP-2640"};
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          const auto diag =
              GpuSafeDiagForTypeImpl(ctx, node.element, active_paths);
          if (!diag.has_value()) {
            SPEC_RULE("GpuSafe-RawPtr");
          }
          return diag;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          const auto diag =
              GpuSafeDiagForTypeImpl(ctx, node.element, active_paths);
          if (!diag.has_value()) {
            SPEC_RULE("GpuSafe-Array");
          }
          return diag;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          for (const auto& elem : node.elements) {
            if (const auto diag =
                    GpuSafeDiagForTypeImpl(ctx, elem, active_paths);
                diag.has_value()) {
              return diag;
            }
          }
          SPEC_RULE("GpuSafe-Tuple");
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          for (const auto& member : node.members) {
            if (const auto diag =
                    GpuSafeDiagForTypeImpl(ctx, member, active_paths);
                diag.has_value()) {
              return diag;
            }
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return GpuSafeDiagForTypeImpl(ctx, node.element, active_paths);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          if (node.state.has_value() && node.state == StringState::View) {
            SPEC_RULE("GpuSafe-StringView");
            return std::nullopt;
          }
          SPEC_RULE("ProhibitedGpuType");
          SPEC_RULE("GpuSafeType-Err");
          return std::optional<std::string_view>{"E-TYP-2640"};
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          if (node.state.has_value() && node.state == BytesState::View) {
            SPEC_RULE("GpuSafe-BytesView");
            return std::nullopt;
          }
          SPEC_RULE("ProhibitedGpuType");
          SPEC_RULE("GpuSafeType-Err");
          return std::optional<std::string_view>{"E-TYP-2640"};
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          if (node.state.has_value() && node.state == PtrState::Valid) {
            SPEC_RULE("ProhibitedGpuType");
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeRange> ||
                             std::is_same_v<T, TypeRangeInclusive> ||
                             std::is_same_v<T, TypeRangeFrom> ||
                             std::is_same_v<T, TypeRangeTo> ||
                             std::is_same_v<T, TypeRangeToInclusive>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return GpuSafeDiagForTypeImpl(ctx, node.base, active_paths);
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeDynamic> ||
                             std::is_same_v<T, TypeModalState>) {
          SPEC_RULE("ProhibitedGpuType");
          SPEC_RULE("GpuSafeType-Err");
          return std::optional<std::string_view>{"E-TYP-2640"};
        } else if constexpr (std::is_same_v<T, TypePathType> ||
                             std::is_same_v<T, TypeApply>) {
          const TypePath& applied_path = node.path;
          const std::vector<TypeRef>& applied_args =
              [&]() -> const std::vector<TypeRef>& {
            if constexpr (std::is_same_v<T, TypePathType>) {
              return node.generic_args;
            } else {
              return node.args;
            }
          }();

          if (const auto* record = LookupRecordDecl(ctx, applied_path)) {
            ast::Path ast_path(applied_path.begin(), applied_path.end());
            const auto key = PathKeyOf(ast_path);
            if (active_paths.find(key) != active_paths.end()) {
              return std::optional<std::string_view>{"E-TYP-2640"};
            }
            const auto args = ResolveDeclGenericArgs(
                ctx, record->generic_params, applied_args);
            if (!args.has_value()) {
              return std::optional<std::string_view>{"E-TYP-2640"};
            }
            active_paths.insert(key);
            const auto diag =
                GpuSafeRecordDiag(ctx, *record, stripped, *args, active_paths);
            active_paths.erase(key);
            return diag;
          }
          if (const auto* enm = LookupEnumDecl(ctx, applied_path)) {
            ast::Path ast_path(applied_path.begin(), applied_path.end());
            const auto key = PathKeyOf(ast_path);
            if (active_paths.find(key) != active_paths.end()) {
              return std::optional<std::string_view>{"E-TYP-2640"};
            }
            const auto args = ResolveDeclGenericArgs(
                ctx, enm->generic_params, applied_args);
            if (!args.has_value()) {
              return std::optional<std::string_view>{"E-TYP-2640"};
            }
            active_paths.insert(key);
            const auto diag = GpuSafeEnumDiag(ctx, *enm, stripped, *args,
                                              active_paths);
            active_paths.erase(key);
            return diag;
          }
          if (LookupModalDecl(ctx, applied_path)) {
            SPEC_RULE("ProhibitedGpuType");
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }

          ast::Path ast_path(applied_path.begin(), applied_path.end());
          const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
          if (it != ctx.sigma.types.end()) {
            if (const auto* alias = std::get_if<ast::TypeAliasDecl>(&it->second)) {
              if (!alias->type) {
                return std::optional<std::string_view>{"E-TYP-2640"};
              }
              const auto lowered = LowerType(ctx, alias->type);
              if (!lowered.ok) {
                return std::optional<std::string_view>{"E-TYP-2640"};
              }
              const auto args = ResolveDeclGenericArgs(
                  ctx, alias->generic_params, applied_args);
              if (!args.has_value()) {
                return std::optional<std::string_view>{"E-TYP-2640"};
              }
              const auto param_names = GenericParamNames(alias->generic_params);
              const auto target =
                  ApplyDeclGenericArgs(lowered.type, param_names, *args);
              return GpuSafeDiagForTypeImpl(ctx, target, active_paths);
            }
          }

          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeFunc> ||
                             std::is_same_v<T, TypeOpaque>) {
          if (!BitcopyType(ctx, stripped)) {
            SPEC_RULE("GpuSafeType-Err");
            return std::optional<std::string_view>{"E-TYP-2640"};
          }
          return std::nullopt;
        } else {
          SPEC_RULE("GpuSafeType-Err");
          return std::optional<std::string_view>{"E-TYP-2640"};
        }
      },
      stripped->node);
}

std::optional<std::string_view> GpuSafeDiagForType(
    const ScopeContext& ctx,
    const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("GpuSafeType");
  SPEC_RULE("GpuSafeComponents");
  std::set<PathKey> active_paths;
  return GpuSafeDiagForTypeImpl(ctx, type, active_paths);
}

// =============================================================================
// EXPORTED: ZeroableType
// =============================================================================

bool ZeroableType(const ScopeContext& ctx, const TypeRef& type) {
  SpecDefsTypePredicates();
  SPEC_RULE("ZeroableType");
  std::set<PathKey> active_paths;
  return ZeroableTypeImpl(ctx, type, active_paths);
}

// =============================================================================
// EXPORTED: EqType
// =============================================================================

bool EqType(const TypeRef& type) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          // Spec (EqType): Numeric primitives + bool + char are equality-comparable.
          return node.name == "i8" || node.name == "i16" ||
                 node.name == "i32" || node.name == "i64" ||
                 node.name == "i128" || node.name == "isize" ||
                 node.name == "u8" || node.name == "u16" ||
                 node.name == "u32" || node.name == "u64" ||
                 node.name == "u128" || node.name == "usize" ||
                 node.name == "f16" || node.name == "f32" ||
                 node.name == "f64" || node.name == "bool" ||
                 node.name == "char";
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return EqType(node.base);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return EqType(node.base);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return true;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return true;
        } else {
          return false;
        }
      },
      type->node);
}

// =============================================================================
// EXPORTED: BuiltinStepType
// =============================================================================

bool BuiltinStepType(const TypeRef& type) {
  SPEC_RULE("BuiltinStepType");
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          return node.name == "i8" || node.name == "i16" ||
                 node.name == "i32" || node.name == "i64" ||
                 node.name == "i128" || node.name == "isize" ||
                 node.name == "u8" || node.name == "u16" ||
                 node.name == "u32" || node.name == "u64" ||
                 node.name == "u128" || node.name == "usize" ||
                 node.name == "char";
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return BuiltinStepType(node.base);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return BuiltinStepType(node.base);
        } else {
          return false;
        }
      },
      type->node);
}

std::optional<FoundationalBuiltinMethodSig> LookupFoundationalBuiltinMethodSig(
    const TypeRef& recv_base,
    std::string_view name) {
  SPEC_RULE("ImplementsEq");
  SPEC_RULE("ImplementsStep");
  if (!recv_base) {
    return std::nullopt;
  }

  const TypeRef base = NormalizeFoundationalBuiltinBase(recv_base);
  if (!base) {
    return std::nullopt;
  }

  FoundationalBuiltinMethodSig sig{};
  sig.recv_perm = Permission::Const;
  sig.recv_type = base;

  if (IdEq(name, "eq") && EqType(base)) {
    sig.params.push_back(TypeFuncParam{std::nullopt, ConstType(base)});
    sig.ret = MakeTypePrim("bool");
    return sig;
  }

  if ((IdEq(name, "successor") || IdEq(name, "predecessor")) &&
      BuiltinStepType(base)) {
    sig.ret = MakeTypeUnion({base, MakeTypePrim("()")});
    return sig;
  }

  return std::nullopt;
}

// =============================================================================
// EXPORTED: OrdType
// =============================================================================

bool OrdType(const TypeRef& type) {
  if (!type) {
    return false;
  }

  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          // Numeric primitives support ordering
          return node.name == "i8" || node.name == "i16" ||
                 node.name == "i32" || node.name == "i64" ||
                 node.name == "i128" || node.name == "isize" ||
                 node.name == "u8" || node.name == "u16" ||
                 node.name == "u32" || node.name == "u64" ||
                 node.name == "u128" || node.name == "usize" ||
                 node.name == "f16" || node.name == "f32" ||
                 node.name == "f64" || node.name == "char";
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return OrdType(node.base);
        } else {
          return false;
        }
      },
      type->node);
}

// =============================================================================
// EXPORTED: CastValid
// =============================================================================

bool CastValid(const TypeRef& source, const TypeRef& target) {
  if (!source || !target) {
    return false;
  }

  const auto src = StripPermAndRefine(source);
  const auto dst = StripPermAndRefine(target);
  const auto* src_prim = src ? std::get_if<TypePrim>(&src->node) : nullptr;
  const auto* dst_prim = dst ? std::get_if<TypePrim>(&dst->node) : nullptr;
  if (!src_prim || !dst_prim) {
    return false;
  }

  const std::string_view s = src_prim->name;
  const std::string_view t = dst_prim->name;

  if (IsNumericPrim(s) && IsNumericPrim(t)) {
    return true;
  }
  if (s == "bool" && IsIntPrim(t)) {
    return true;
  }
  if (IsIntPrim(s) && t == "bool") {
    return true;
  }
  if (s == "char" && t == "u32") {
    return true;
  }
  if (s == "u32" && t == "char") {
    return true;
  }
  return false;
}

// =============================================================================
// EXPORTED: StripPerm
// =============================================================================

TypeRef StripPerm(const TypeRef& type) {
  if (!type) {
    return type;
  }

  return std::visit(
      [&](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return StripPerm(node.base);
        }
        return type;
      },
      type->node);
}

}  // namespace cursive::analysis
