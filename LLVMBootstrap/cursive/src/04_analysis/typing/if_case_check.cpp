// =============================================================================
// if_case_check.cpp - if-is typing and exhaustiveness
// =============================================================================

#include "04_analysis/typing/if_case_check.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/process_config.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_pattern.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/subtyping.h"

namespace cursive::analysis {

namespace {

struct IntroResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeEnv env;
};

struct LocalTypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

struct AllEqResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool equal = false;
};

struct ExhaustiveResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool exhaustive = false;
};

struct IfCaseClauseLabel {
  enum class Kind {
    Enum,
    Modal,
    Union,
  };

  Kind kind = Kind::Enum;
  TypePath path;
  IdKey name;
  TypeRef type;
};

struct IfCaseClauseLabelResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<IfCaseClauseLabel> label;
};

struct IfCaseClauseLabelEqualResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool equal = false;
};

struct IfCaseUnreachableResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  bool unreachable = false;
};

static inline void SpecDefsIfCase() {
  SPEC_DEF("CaseJudg", "5.2.13");
  SPEC_DEF("CaseScope", "17.5.4");
  SPEC_DEF("CaseScope-Narrow", "17.5.4");
  SPEC_DEF("CaseScope-PatternOnly", "17.5.4");
  SPEC_DEF("PatternNarrow", "17.5.4");
  SPEC_DEF("PatternNarrow-Perm", "17.5.4");
  SPEC_DEF("PatternNarrow-ModalRef", "17.5.4");
  SPEC_DEF("PatternNarrow-ModalState", "17.5.4");
  SPEC_DEF("PatternNarrow-Union", "17.5.4");
  SPEC_DEF("T-IfIs", "5.2.13");
  SPEC_DEF("T-IfIs-No-Else", "5.2.13");
  SPEC_DEF("T-IfCase-Enum", "5.2.13");
  SPEC_DEF("T-IfCase-Modal", "5.2.13");
  SPEC_DEF("T-IfCase-Union", "5.2.13");
  SPEC_DEF("T-IfCase-Other", "5.2.13");
  SPEC_DEF("Chk-IfIs", "5.2.13");
  SPEC_DEF("Chk-IfIs-No-Else", "5.2.13");
  SPEC_DEF("Chk-IfCase-Enum", "5.2.13");
  SPEC_DEF("Chk-IfCase-Modal", "5.2.13");
  SPEC_DEF("Chk-IfCase-Union", "5.2.13");
  SPEC_DEF("Chk-IfCase-Other", "5.2.13");
  SPEC_DEF("IfCase-Unreachable", "5.2.13");
  SPEC_DEF("IfCase-Enum-NonExhaustive", "5.2.13");
  SPEC_DEF("IfCase-Modal-NonExhaustive", "5.2.13");
  SPEC_DEF("IfCase-Union-NonExhaustive", "5.2.13");
  SPEC_DEF("ArmBody", "5.2.13");
  SPEC_DEF("Irrefutable", "5.2.13");
  SPEC_DEF("HasIrrefutableArm", "5.2.13");
  SPEC_DEF("ArmVariants", "5.2.13");
  SPEC_DEF("VariantNames", "5.2.13");
  SPEC_DEF("ArmStates", "5.2.13");
  SPEC_DEF("StateNames", "5.2.13");
  SPEC_DEF("UnionTypesExhaustive", "5.2.13");
  SPEC_DEF("Unguarded", "5.2.13");
  SPEC_DEF("ArmCaseLabel", "5.2.13");
  SPEC_DEF("ArmUnreachable", "5.2.13");
  SPEC_DEF("AllEq", "5.2.13");
  SPEC_DEF("IntroAll", "5.2.11");
  SPEC_DEF("IntroAllVar", "5.2.11");
  SPEC_DEF("StripPerm", "5.2.12");
}

static bool IsUnitType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  const TypeRef stripped = StripPerm(type);
  if (!stripped) {
    return false;
  }
  const auto* prim = std::get_if<TypePrim>(&stripped->node);
  return prim && prim->name == "()";
}

static TypeRef StripPermOnceLocal(const TypeRef& type) {
  if (!type) {
    return type;
  }
  if (const auto* perm = std::get_if<TypePerm>(&type->node)) {
    return perm->base;
  }
  return type;
}

static TypeEquivResult TypeEquivIgnorePerm(const TypeRef& lhs, const TypeRef& rhs) {
  return TypeEquiv(StripPermOnceLocal(lhs), StripPermOnceLocal(rhs));
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

static const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                                     const TypePath& path) {
  if (path.empty()) {
    return nullptr;
  }
  if (path.size() > 1) {
    ast::Path full;
    full.reserve(path.size());
    for (const auto& seg : path) {
      full.push_back(seg);
    }
    const auto it = ctx.sigma.types.find(PathKeyOf(full));
    if (it == ctx.sigma.types.end()) {
      return nullptr;
    }
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

static AliasExpandResult ExpandTypeAliasApply(const ScopeContext& ctx,
                                              const TypePathType& applied) {
  AliasExpandResult result;
  const auto* alias = LookupTypeAliasDecl(ctx, applied.path);
  if (!alias) {
    return result;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    result.ok = false;
    result.diag_id = lowered.diag_id;
    return result;
  }

  if (!alias->generic_params.has_value()) {
    if (!applied.generic_args.empty()) {
      return result;
    }
    result.type = lowered.type;
    result.expanded = true;
    return result;
  }

  const auto& params = alias->generic_params->params;
  if (applied.generic_args.size() > params.size()) {
    return result;
  }

  const auto subst = BuildSubstitution(params, applied.generic_args);
  result.type = InstantiateType(lowered.type, subst);
  result.expanded = result.type != nullptr;
  return result;
}

static AliasExpandResult NormalizeAliasTopLevel(const ScopeContext& ctx,
                                                const TypeRef& type) {
  AliasExpandResult out;
  out.type = type;
  for (int i = 0; i < 16; ++i) {
    if (!out.type) {
      return out;
    }
	    const auto* path = AppliedTypePath(*out.type);
	    const auto* args = AppliedTypeArgs(*out.type);
	    if (!path || !args) {
	      return out;
	    }
	    const auto expanded = ExpandTypeAliasApply(ctx, TypePathType{*path, *args});
    if (!expanded.ok) {
      out.ok = false;
      out.diag_id = expanded.diag_id;
      return out;
    }
    if (!expanded.expanded) {
      return out;
    }
    out.type = expanded.type;
    out.expanded = true;
  }
  return out;
}

static std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  return ParamMode::Move;
}

static std::optional<BytesState> LowerBytesState(
    const std::optional<ast::BytesState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::BytesState::Managed:
      return BytesState::Managed;
    case ast::BytesState::View:
      return BytesState::View;
  }
  return std::nullopt;
}

static std::optional<StringState> LowerStringState(
    const std::optional<ast::StringState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::StringState::Managed:
      return StringState::Managed;
    case ast::StringState::View:
      return StringState::View;
  }
  return std::nullopt;
}

static std::optional<PtrState> LowerPtrState(
    const std::optional<ast::PtrState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case ast::PtrState::Valid:
      return PtrState::Valid;
    case ast::PtrState::Null:
      return PtrState::Null;
    case ast::PtrState::Expired:
      return PtrState::Expired;
  }
  return std::nullopt;
}

static RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual) {
  switch (qual) {
    case ast::RawPtrQual::Imm:
      return RawPtrQual::Imm;
    case ast::RawPtrQual::Mut:
      return RawPtrQual::Mut;
  }
  return RawPtrQual::Imm;
}

static Permission LowerPermission(ast::TypePerm perm) {
  switch (perm) {
    case ast::TypePerm::Const:
      return Permission::Const;
    case ast::TypePerm::Unique:
      return Permission::Unique;
    case ast::TypePerm::Shared:
      return Permission::Shared;
  }
  return Permission::Const;
}

static LocalTypeLowerResult LocalLowerType(const ScopeContext& ctx,
                                 const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return {false, std::nullopt, {}};
  }
  return std::visit(
      [&](const auto& node) -> LocalTypeLowerResult {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::TypePrim>) {
          return {true, std::nullopt, MakeTypePrim(node.name)};
        } else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          const auto base = LocalLowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypePerm(LowerPermission(node.perm), base.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          std::vector<TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& elem : node.types) {
            const auto lowered = LocalLowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            members.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeUnion(std::move(members))};
        } else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          std::vector<TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LocalLowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            params.push_back(TypeFuncParam{LowerParamMode(param.mode),
                                           lowered.type});
          }
          const auto ret = LocalLowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          return {true, std::nullopt, MakeTypeFunc(std::move(params), ret.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeClosure>) {
          std::vector<std::pair<bool, TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LocalLowerType(ctx, param.type);
            if (!lowered.ok) {
              return lowered;
            }
            const bool is_move =
                param.mode.has_value() && *param.mode == ast::ParamMode::Move;
            params.emplace_back(is_move, lowered.type);
          }
          const auto ret = LocalLowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          std::optional<std::vector<SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto dep_type = LocalLowerType(ctx, dep.type);
              if (!dep_type.ok) {
                return dep_type;
              }
              SharedDep lowered_dep;
              lowered_dep.name = dep.name;
              lowered_dep.type = dep_type.type;
              deps.push_back(std::move(lowered_dep));
            }
            deps_opt = std::move(deps);
          }
          return {true, std::nullopt,
                  MakeTypeClosure(std::move(params), ret.type,
                                  std::move(deps_opt))};
        } else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          std::vector<TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto lowered = LocalLowerType(ctx, elem);
            if (!lowered.ok) {
              return lowered;
            }
            elements.push_back(lowered.type);
          }
          return {true, std::nullopt, MakeTypeTuple(std::move(elements))};
        } else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          const auto len = ConstLen(ctx, node.length);
          if (!len.ok || !len.value.has_value()) {
            return {false, len.diag_id, {}};
          }
          return {true, std::nullopt, MakeTypeArray(elem.type, *len.value)};
        } else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt, MakeTypeSlice(elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypePtr(elem.type, LowerPtrState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          const auto elem = LocalLowerType(ctx, node.element);
          if (!elem.ok) {
            return elem;
          }
          return {true, std::nullopt,
                  MakeTypeRawPtr(LowerRawPtrQual(node.qual), elem.type)};
        } else if constexpr (std::is_same_v<T, ast::TypeString>) {
          return {true, std::nullopt, MakeTypeString(LowerStringState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          return {true, std::nullopt, MakeTypeBytes(LowerBytesState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeDynamic>) {
          return {true, std::nullopt, MakeTypeDynamic(node.path)};
        } else if constexpr (std::is_same_v<T, ast::TypeOpaque>) {
          return {true, std::nullopt,
                  MakeTypeOpaque(node.path, type.get(), type->span)};
        } else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          const auto base = LocalLowerType(ctx, node.base);
          if (!base.ok) {
            return base;
          }
          return {true, std::nullopt,
                  MakeTypeRefine(base.type, node.predicate)};
        } else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          std::vector<TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LocalLowerType(ctx, arg);
            if (!lowered.ok) {
              return lowered;
            }
            args.push_back(lowered.type);
          }
          return {true, std::nullopt,
                  MakeTypeModalState(node.path, node.state, std::move(args))};
        } else if constexpr (std::is_same_v<T, ast::TypePathType>) {
          // Generic type instantiation lowering
          // Per WF-Apply, type arguments MUST be preserved
          if (!node.generic_args.empty()) {
            std::vector<TypeRef> lowered_args;
            lowered_args.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              const auto lower_result = LocalLowerType(ctx, arg);
              if (!lower_result.ok) {
                return lower_result;
              }
              lowered_args.push_back(lower_result.type);
            }
            return {true, std::nullopt,
                    MakeTypePath(node.path, std::move(lowered_args))};
          }
          return {true, std::nullopt, MakeTypePath(node.path)};
        } else {
          return {false, std::nullopt, {}};
        }
      },
      type->node);
}

static const ast::EnumDecl* LookupEnumDecl(const ScopeContext& ctx,
                                              const TypePath& path) {
  ast::TypePath ast_path;
  ast_path.reserve(path.size());
  for (const auto& comp : path) {
    ast_path.push_back(comp);
  }
  const auto it = ctx.sigma.types.find(PathKeyOf(ast_path));
  if (it == ctx.sigma.types.end()) {
    return nullptr;
  }
  return std::get_if<ast::EnumDecl>(&it->second);
}

static bool InScope(const TypeEnv& env, const IdKey& key) {
  if (env.scopes.empty()) {
    return false;
  }
  return env.scopes.back().find(key) != env.scopes.back().end();
}

static bool InOuter(const TypeEnv& env, const IdKey& key) {
  if (env.scopes.size() < 2) {
    return false;
  }
  for (std::size_t i = 0; i + 1 < env.scopes.size(); ++i) {
    if (env.scopes[i].find(key) != env.scopes[i].end()) {
      return true;
    }
  }
  return false;
}

static IntroResult IntroBinding(const TypeEnv& env,
                                std::string_view name,
                                const TypeBinding& binding) {
  SpecDefsIfCase();
  if (ReservedGen(name)) {
    SPEC_RULE("Intro-Reserved-Gen-Err");
    return {false, "Intro-Reserved-Gen-Err", env};
  }

  const auto key = IdKeyOf(name);
  if (env.scopes.empty()) {
    return {false, std::nullopt, env};
  }

  if (InScope(env, key)) {
    SPEC_RULE("Intro-Dup");
    return {false, std::nullopt, env};
  }
  if (InOuter(env, key)) {
    SPEC_RULE("Intro-Outer-Err");
    if (core::IsDebugEnabled("shadow")) {
      std::cerr << "[cursive] outer name reuse rejected for pattern `" << name << "`";
      for (std::size_t i = 1; i < env.scopes.size(); ++i) {
        if (env.scopes[i].count(key)) {
          std::cerr << " (outer scope " << i << ")";
          break;
        }
      }
      std::cerr << "\n";
    }
    return {false, "Intro-Outer-Err", env};
  }

  TypeEnv out = env;
  out.scopes.back().emplace(key, binding);
  SPEC_RULE("Intro-Ok");
  return {true, std::nullopt, std::move(out)};
}

static IntroResult IntroAll(const TypeEnv& env,
                            const std::vector<std::pair<std::string, TypeRef>>& binds,
                            ast::Mutability mut) {
  SpecDefsIfCase();
  if (binds.empty()) {
    if (mut == ast::Mutability::Var) {
      SPEC_RULE("IntroAllVar-Empty");
    } else {
      SPEC_RULE("IntroAll-Empty");
    }
    return {true, std::nullopt, env};
  }

  TypeEnv current = env;
  for (const auto& [name, type] : binds) {
    TypeBinding binding{mut, type};
    const auto res = IntroBinding(current, name, binding);
    if (!res.ok) {
      return res;
    }
    current = res.env;
    if (mut == ast::Mutability::Var) {
      SPEC_RULE("IntroAllVar-Cons");
    } else {
      SPEC_RULE("IntroAll-Cons");
    }
  }

  return {true, std::nullopt, std::move(current)};
}

struct PatternMatchedTypeResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool matched = false;
  TypeRef type;
};

static std::optional<IdKey> ScrutineeIdentifier(
    const ast::ExprPtr& scrutinee) {
  if (!scrutinee) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&scrutinee->node)) {
    return IdKeyOf(ident->name);
  }
  return std::nullopt;
}

static TypeEnv RefineScrutineeEnv(const ast::ExprPtr& scrutinee,
                                  const TypeEnv& env,
                                  const TypeRef& refined_type) {
  const auto ident = ScrutineeIdentifier(scrutinee);
  if (!ident.has_value() || !refined_type) {
    return env;
  }

  TypeEnv out = env;
  for (auto scope_it = out.scopes.rbegin(); scope_it != out.scopes.rend();
       ++scope_it) {
    auto binding_it = scope_it->find(*ident);
    if (binding_it == scope_it->end()) {
      continue;
    }
    binding_it->second.type = refined_type;
    return out;
  }
  return out;
}

static PatternMatchedTypeResult ConcreteModalPatternType(
    const ScopeContext& ctx,
    const ast::PatternPtr& pattern,
    const TypeRef& expected) {
  PatternMatchedTypeResult result;
  if (!pattern || !expected) {
    return result;
  }
  const auto* modal_pattern = std::get_if<ast::ModalPattern>(&pattern->node);
  if (!modal_pattern) {
    result.matched = true;
    result.type = expected;
    return result;
  }

  const TypeRef expected_base = StripPermOnceLocal(expected);
  if (!expected_base) {
    return result;
  }

  if (const auto* modal_state =
          std::get_if<TypeModalState>(&expected_base->node)) {
    result.matched = true;
    result.type = expected;
    SPEC_RULE("PatternNarrow-ModalState");
    return result;
  }

  const auto* path_type = AppliedTypePath(*expected_base);
  const auto* path_args = AppliedTypeArgs(*expected_base);
  if (!path_type) {
    return result;
  }

  ast::TypePath ast_path;
  ast_path.reserve(path_type->size());
  for (const auto& comp : *path_type) {
    ast_path.push_back(comp);
  }
  const ast::ModalDecl* modal_decl = LookupModalDecl(ctx, ast_path);
  if (!modal_decl || !HasState(*modal_decl, modal_pattern->state)) {
    return result;
  }

  result.matched = true;
  result.type = MakeTypeModalState(*path_type, modal_pattern->state,
                                   path_args ? *path_args
                                             : std::vector<TypeRef>{});
  SPEC_RULE("PatternNarrow-ModalRef");
  return result;
}

static PatternMatchedTypeResult PatternMatchedType(
    const ScopeContext& ctx,
    const ast::PatternPtr& pattern,
    const TypeRef& expected) {
  PatternMatchedTypeResult result;
  if (!pattern || !expected) {
    return result;
  }

  if (const auto* perm = std::get_if<TypePerm>(&expected->node)) {
    const auto narrowed = PatternMatchedType(ctx, pattern, perm->base);
    if (!narrowed.ok || !narrowed.matched) {
      return narrowed;
    }
    SPEC_RULE("PatternNarrow-Perm");
    result.matched = true;
    result.type = MakeTypePerm(perm->perm, narrowed.type);
    return result;
  }

  const auto normalized = NormalizeAliasTopLevel(ctx, expected);
  if (!normalized.ok) {
    result.ok = false;
    result.diag_id = normalized.diag_id;
    return result;
  }
  if (normalized.expanded && normalized.type) {
    return PatternMatchedType(ctx, pattern, normalized.type);
  }

  const TypeRef expected_base = StripPermOnceLocal(normalized.type);
  if (!expected_base) {
    return result;
  }

  if (const auto* union_type = std::get_if<TypeUnion>(&expected_base->node)) {
    std::vector<TypeRef> matched_members;
    std::optional<std::string_view> first_diag;
    for (const auto& member : union_type->members) {
      const auto member_narrow = PatternMatchedType(ctx, pattern, member);
      if (!member_narrow.ok) {
        if (member_narrow.diag_id.has_value() && !first_diag.has_value()) {
          first_diag = member_narrow.diag_id;
        }
        continue;
      }
      if (member_narrow.matched && member_narrow.type) {
        matched_members.push_back(member_narrow.type);
      }
    }

    if (matched_members.empty()) {
      result.ok = !first_diag.has_value();
      result.diag_id = first_diag;
      return result;
    }

    result.matched = true;
    result.type = matched_members.size() == 1
                      ? matched_members.front()
                      : MakeTypeUnion(std::move(matched_members));
    SPEC_RULE("PatternNarrow-Union");
    return result;
  }

  const auto pattern_result = TypePatternAgainstType(ctx, pattern, expected_base);
  if (!pattern_result.ok) {
    result.ok = false;
    result.diag_id = pattern_result.diag_id;
    return result;
  }

  if (const auto* typed = std::get_if<ast::TypedPattern>(&pattern->node)) {
    const auto lowered = LocalLowerType(ctx, typed->type);
    if (!lowered.ok) {
      result.ok = false;
      result.diag_id = lowered.diag_id;
      return result;
    }
    result.matched = true;
    result.type = lowered.type;
    return result;
  }

  return ConcreteModalPatternType(ctx, pattern, expected);
}

static IntroResult CaseScopeEnv(const ScopeContext& ctx,
                                const ast::ExprPtr& scrutinee,
                                const TypeEnv& env,
                                const ast::PatternPtr& pattern,
                                const TypeRef& scrutinee_type,
                                ast::Mutability mut) {
  SpecDefsIfCase();
  const auto pat = TypePatternAgainstType(ctx, pattern, scrutinee_type);
  if (!pat.ok) {
    return {false, pat.diag_id, env};
  }

  TypeEnv case_base = env;
  const auto matched_type = PatternMatchedType(ctx, pattern, scrutinee_type);
  if (!matched_type.ok) {
    return {false, matched_type.diag_id, env};
  }
  if (matched_type.matched && matched_type.type &&
      ScrutineeIdentifier(scrutinee).has_value()) {
    case_base = RefineScrutineeEnv(scrutinee, env, matched_type.type);
    SPEC_RULE("CaseScope-Narrow");
  } else {
    SPEC_RULE("CaseScope-PatternOnly");
  }

  TypeEnv binding_scope = PushScope(case_base);
  return IntroAll(binding_scope, pat.bindings, mut);
}

static std::unordered_set<IdKey> VariantNames(const ast::EnumDecl& decl) {
  SpecDefsIfCase();
  std::unordered_set<IdKey> out;
  for (const auto& variant : decl.variants) {
    out.insert(IdKeyOf(variant.name));
  }
  return out;
}

static std::unordered_set<IdKey> ArmVariants(
    const std::vector<ast::IfCaseClause>& arms,
    const TypePath& expected_path) {
  SpecDefsIfCase();
  std::unordered_set<IdKey> out;
  for (const auto& arm : arms) {
    if (!arm.pattern) {
      continue;
    }
    if (const auto* enum_pat = std::get_if<ast::EnumPattern>(&arm.pattern->node)) {
      if (enum_pat->path == expected_path) {
        out.insert(IdKeyOf(enum_pat->name));
      }
    }
  }
  return out;
}

static std::unordered_set<IdKey> ArmStates(
    const ScopeContext& ctx,
    const std::vector<ast::IfCaseClause>& arms) {
  SpecDefsIfCase();
  std::unordered_set<IdKey> out;
  for (const auto& arm : arms) {
    if (!arm.pattern) {
      continue;
    }
    if (const auto* modal_pat = std::get_if<ast::ModalPattern>(&arm.pattern->node)) {
      out.insert(IdKeyOf(modal_pat->state));
      continue;
    }
    if (const auto* typed = std::get_if<ast::TypedPattern>(&arm.pattern->node)) {
      const auto lowered = LocalLowerType(ctx, typed->type);
      if (!lowered.ok || !lowered.type) {
        continue;
      }
      const TypeRef lowered_base = StripPermOnceLocal(lowered.type);
      if (!lowered_base) {
        continue;
      }
      if (const auto* modal_state =
              std::get_if<TypeModalState>(&lowered_base->node)) {
        out.insert(IdKeyOf(modal_state->state));
      }
    }
  }
  return out;
}

static bool HasIrrefutableArm(const ScopeContext& ctx,
                              const std::vector<ast::IfCaseClause>& arms,
                              const TypeRef& expected) {
  SpecDefsIfCase();
  for (const auto& arm : arms) {
    if (IrrefutablePattern(ctx, arm.pattern, expected)) {
      return true;
    }
  }
  return false;
}

static bool Unguarded(const ast::IfCaseClause& arm) {
  SpecDefsIfCase();
  (void)arm;
  return true;
}

static IfCaseClauseLabelResult ArmCaseLabelOf(const ScopeContext& ctx,
                                              const TypeRef& scrutinee,
                                              const ast::PatternPtr& pattern) {
  SpecDefsIfCase();
  IfCaseClauseLabelResult result{true, std::nullopt, std::nullopt};
  if (!pattern || !scrutinee) {
    return result;
  }

  const TypeRef scrutinee_base = StripPermOnceLocal(scrutinee);
  if (!scrutinee_base) {
    return result;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (const auto* union_type =
                  std::get_if<TypeUnion>(&scrutinee_base->node)) {
            for (const auto& member : union_type->members) {
              const auto member_pattern =
                  TypePatternAgainstType(ctx, pattern, member);
              if (!member_pattern.ok) {
                if (member_pattern.diag_id.has_value()) {
                  result.ok = false;
                  result.diag_id = member_pattern.diag_id;
                  return;
                }
                continue;
              }
              IfCaseClauseLabel label;
              label.kind = IfCaseClauseLabel::Kind::Union;
              label.type = member;
              result.label = std::move(label);
              return;
            }
          }
          IfCaseClauseLabel label;
          label.kind = IfCaseClauseLabel::Kind::Enum;
          label.path = node.path;
          label.name = IdKeyOf(node.name);
          result.label = std::move(label);
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (const auto* union_type =
                  std::get_if<TypeUnion>(&scrutinee_base->node)) {
            for (const auto& member : union_type->members) {
              const auto member_pattern =
                  TypePatternAgainstType(ctx, pattern, member);
              if (!member_pattern.ok) {
                if (member_pattern.diag_id.has_value()) {
                  result.ok = false;
                  result.diag_id = member_pattern.diag_id;
                  return;
                }
                continue;
              }
              IfCaseClauseLabel label;
              label.kind = IfCaseClauseLabel::Kind::Union;
              label.type = member;
              result.label = std::move(label);
              return;
            }
          }
          IfCaseClauseLabel label;
          label.kind = IfCaseClauseLabel::Kind::Modal;
          label.name = IdKeyOf(node.state);
          result.label = std::move(label);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          const auto* union_type = std::get_if<TypeUnion>(&scrutinee_base->node);
          if (!union_type) {
            return;
          }
          const auto lowered = LocalLowerType(ctx, node.type);
          if (!lowered.ok || !lowered.type) {
            result.ok = false;
            result.diag_id = lowered.diag_id;
            return;
          }
          for (const auto& member : union_type->members) {
            const auto equiv = TypeEquivIgnorePerm(lowered.type, member);
            if (!equiv.ok) {
              result.ok = false;
              result.diag_id = equiv.diag_id;
              return;
            }
            if (equiv.equiv) {
              IfCaseClauseLabel label;
              label.kind = IfCaseClauseLabel::Kind::Union;
              label.type = member;
              result.label = std::move(label);
              return;
            }
          }
        }
      },
      pattern->node);

  return result;
}

static IfCaseClauseLabelEqualResult ArmCaseLabelEqual(
    const IfCaseClauseLabel& lhs,
    const IfCaseClauseLabel& rhs) {
  SpecDefsIfCase();
  IfCaseClauseLabelEqualResult result{true, std::nullopt, false};
  if (lhs.kind != rhs.kind) {
    return result;
  }

  switch (lhs.kind) {
    case IfCaseClauseLabel::Kind::Enum:
      result.equal = lhs.path == rhs.path && lhs.name == rhs.name;
      return result;
    case IfCaseClauseLabel::Kind::Modal:
      result.equal = lhs.name == rhs.name;
      return result;
    case IfCaseClauseLabel::Kind::Union: {
      const auto equiv = TypeEquivIgnorePerm(lhs.type, rhs.type);
      if (!equiv.ok) {
        result.ok = false;
        result.diag_id = equiv.diag_id;
        return result;
      }
      result.equal = equiv.equiv;
      return result;
    }
  }

  return result;
}

static IfCaseUnreachableResult ArmUnreachable(
    const ScopeContext& ctx,
    const TypeRef& scrutinee,
    const std::vector<ast::IfCaseClause>& arms,
    std::size_t index) {
  SpecDefsIfCase();
  IfCaseUnreachableResult result{true, std::nullopt, false};
  if (!scrutinee || index >= arms.size()) {
    return result;
  }

  for (std::size_t prev = 0; prev < index; ++prev) {
    if (!arms[prev].pattern) {
      continue;
    }
    if (Unguarded(arms[prev]) &&
        IrrefutablePattern(ctx, arms[prev].pattern, scrutinee)) {
      result.unreachable = true;
      return result;
    }
  }

  if (!Unguarded(arms[index]) || !arms[index].pattern) {
    return result;
  }

  const auto current_label = ArmCaseLabelOf(ctx, scrutinee, arms[index].pattern);
  if (!current_label.ok) {
    result.ok = false;
    result.diag_id = current_label.diag_id;
    return result;
  }
  if (!current_label.label.has_value()) {
    return result;
  }

  for (std::size_t prev = 0; prev < index; ++prev) {
    if (!Unguarded(arms[prev]) || !arms[prev].pattern) {
      continue;
    }
    const auto prev_label = ArmCaseLabelOf(ctx, scrutinee, arms[prev].pattern);
    if (!prev_label.ok) {
      result.ok = false;
      result.diag_id = prev_label.diag_id;
      return result;
    }
    if (!prev_label.label.has_value()) {
      continue;
    }
    const auto equal =
        ArmCaseLabelEqual(*prev_label.label, *current_label.label);
    if (!equal.ok) {
      result.ok = false;
      result.diag_id = equal.diag_id;
      return result;
    }
    if (equal.equal) {
      result.unreachable = true;
      return result;
    }
  }

  return result;
}

static IfCaseUnreachableResult FindUnreachableArm(
    const ScopeContext& ctx,
    const TypeRef& scrutinee,
    const std::vector<ast::IfCaseClause>& arms) {
  SpecDefsIfCase();
  IfCaseUnreachableResult result{true, std::nullopt, false};
  for (std::size_t i = 0; i < arms.size(); ++i) {
    const auto arm_result = ArmUnreachable(ctx, scrutinee, arms, i);
    if (!arm_result.ok) {
      return arm_result;
    }
    if (arm_result.unreachable) {
      return arm_result;
    }
  }
  return result;
}

static AllEqResult AllEq(const std::vector<TypeRef>& types) {
  SpecDefsIfCase();
  if (types.empty()) {
    return {true, std::nullopt, false};
  }
  TypeRef base = types.front();
  for (std::size_t i = 1; i < types.size(); ++i) {
    const auto equiv = TypeEquiv(base, types[i]);
    if (!equiv.ok) {
      return {false, equiv.diag_id, false};
    }
    if (!equiv.equiv) {
      return {true, std::nullopt, false};
    }
  }
  return {true, std::nullopt, true};
}

static ExhaustiveResult UnionTypesExhaustive(
    const ScopeContext& ctx,
    const std::vector<ast::IfCaseClause>& arms,
    const std::vector<TypeRef>& members) {
  SpecDefsIfCase();
  for (const auto& member : members) {
    bool found = false;
    for (const auto& arm : arms) {
      if (!arm.pattern) {
        continue;
      }
      const auto pattern = TypePatternAgainstType(ctx, arm.pattern, member);
      if (!pattern.ok) {
        if (pattern.diag_id.has_value()) {
          return {false, pattern.diag_id, false};
        }
        continue;
      }
      found = true;
      break;
    }
    if (!found) {
      return {true, std::nullopt, false};
    }
  }
  return {true, std::nullopt, true};
}

static ExprTypeResult TypeArmBody(const ScopeContext& ctx,
                                  const StmtTypeContext& type_ctx,
                                  const ast::ExprPtr& body,
                                  const TypeEnv& env) {
  SpecDefsIfCase();
  if (!body) {
    return {false, std::nullopt, {}};
  }

  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&body->node)) {
    if (!block_expr->block) {
      return {false, std::nullopt, {}};
    }
    TypeEnv live_env = env;
    StmtTypeContext arm_ctx = type_ctx;
    arm_ctx.env_ref = &live_env;
    auto active_env = [&]() -> const TypeEnv& {
      return arm_ctx.env_ref ? *arm_ctx.env_ref : env;
    };
    auto type_expr = [&](const ast::ExprPtr& inner) {
      return TypeExpr(ctx, arm_ctx, inner, active_env());
    };
    auto type_ident = [&](std::string_view name) -> ExprTypeResult {
      return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)},
                                active_env());
    };
    auto type_place = [&](const ast::ExprPtr& inner) {
      return TypePlace(ctx, arm_ctx, inner, active_env());
    };
    const auto typed =
        TypeBlock(ctx, arm_ctx, *block_expr->block, env, type_expr,
                  type_ident, type_place, arm_ctx.env_ref);
    if (!typed.ok) {
      return typed;
    }
    SPEC_RULE("ArmBody-Block");
    return typed;
  }

  const auto typed = TypeExpr(ctx, type_ctx, body, env);
  if (!typed.ok) {
    return typed;
  }
  SPEC_RULE("ArmBody-Expr");
  return typed;
}

static CheckResult CheckArmBody(const ScopeContext& ctx,
                                const StmtTypeContext& type_ctx,
                                const ast::ExprPtr& body,
                                const TypeEnv& env,
                                const TypeRef& expected) {
  SpecDefsIfCase();
  CheckResult result;
  if (!body || !expected) {
    return result;
  }

  if (const auto* block_expr = std::get_if<ast::BlockExpr>(&body->node)) {
    if (!block_expr->block) {
      return result;
    }
    TypeEnv live_env = env;
    StmtTypeContext arm_ctx = type_ctx;
    arm_ctx.env_ref = &live_env;
    auto active_env = [&]() -> const TypeEnv& {
      return arm_ctx.env_ref ? *arm_ctx.env_ref : env;
    };
    auto type_expr = [&](const ast::ExprPtr& inner) {
      return TypeExpr(ctx, arm_ctx, inner, active_env());
    };
    auto type_ident = [&](std::string_view name) -> ExprTypeResult {
      return TypeIdentifierExpr(ctx, ast::IdentifierExpr{std::string(name)},
                                active_env());
    };
    auto type_place = [&](const ast::ExprPtr& inner) {
      return TypePlace(ctx, arm_ctx, inner, active_env());
    };
    const auto check =
        CheckBlock(ctx, arm_ctx, *block_expr->block, env, expected, type_expr,
                   type_ident, type_place, arm_ctx.env_ref);
    if (!check.ok) {
      result.diag_id = check.diag_id;
      return result;
    }
    SPEC_RULE("ArmBody-Block-Chk");
    result.ok = true;
    return result;
  }

  const auto check = CheckExprAgainst(ctx, type_ctx, body, expected, env);
  if (!check.ok) {
    result.diag_id = check.diag_id;
    return result;
  }
  SPEC_RULE("ArmBody-Expr-Chk");
  result.ok = true;
  return result;
}

}  // namespace

ExprTypeResult TypeIfIsExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::IfIsExpr& expr,
                           const TypeEnv& env) {
  SpecDefsIfCase();
  ExprTypeResult result;
  if (!expr.scrutinee || !expr.pattern || !expr.then_expr) {
    return result;
  }

  const auto scrutinee = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.scrutinee,
      env);
  if (!scrutinee.ok) {
    result.diag_id = scrutinee.diag_id;
    return result;
  }

  const auto scrutinee_norm = NormalizeAliasTopLevel(ctx, StripPerm(scrutinee.type));
  if (!scrutinee_norm.ok) {
    result.diag_id = scrutinee_norm.diag_id;
    return result;
  }
  const TypeRef scrutinee_base = scrutinee_norm.type;
  if (!scrutinee_base) {
    return result;
  }

  const auto case_scope = CaseScopeEnv(ctx,
                                       expr.scrutinee,
                                       env,
                                       expr.pattern,
                                       scrutinee_base,
                                       ast::Mutability::Let);
  if (!case_scope.ok) {
    result.diag_id = case_scope.diag_id;
    return result;
  }
  const TypeEnv then_env = case_scope.env;

  const bool has_else = static_cast<bool>(expr.else_expr);
  const TypeRef unit_type = MakeTypePrim("()");
  if (!has_else) {
    const auto unit_check =
        CheckArmBody(ctx, type_ctx, expr.then_expr, then_env, unit_type);
    if (!unit_check.ok) {
      result.diag_id = unit_check.diag_id;
      return result;
    }
    SPEC_RULE("T-IfIs-No-Else");
    result.ok = true;
    result.type = unit_type;
    return result;
  }

  const auto then_typed = TypeArmBody(ctx, type_ctx, expr.then_expr, then_env);
  if (!then_typed.ok) {
    result.diag_id = then_typed.diag_id;
    return result;
  }
  const auto else_typed = TypeExpr(ctx, type_ctx, expr.else_expr, env);
  if (!else_typed.ok) {
    result.diag_id = else_typed.diag_id;
    return result;
  }

  const auto all_eq = AllEq({then_typed.type, else_typed.type});
  if (!all_eq.ok) {
    result.diag_id = all_eq.diag_id;
    return result;
  }
  if (!all_eq.equal) {
    SPEC_RULE("If-Branch-Mismatch");
    result.diag_id = "If-Branch-Mismatch";
    return result;
  }

  SPEC_RULE("T-IfIs");
  result.ok = true;
  result.type = then_typed.type;
  return result;
}

ExprTypeResult TypeIfCaseExpr(const ScopeContext& ctx,
                             const StmtTypeContext& type_ctx,
                             const ast::IfCaseExpr& expr,
                             const TypeEnv& env) {
  SpecDefsIfCase();
  ExprTypeResult result;
  if (!expr.scrutinee) {
    return result;
  }

  const auto scrutinee = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.scrutinee,
      env);
  if (!scrutinee.ok) {
    result.diag_id = scrutinee.diag_id;
    return result;
  }

  const auto scrutinee_norm = NormalizeAliasTopLevel(ctx, StripPerm(scrutinee.type));
  if (!scrutinee_norm.ok) {
    result.diag_id = scrutinee_norm.diag_id;
    return result;
  }
  const TypeRef scrutinee_base = scrutinee_norm.type;
  if (!scrutinee_base) {
    return result;
  }
  const TypeRef scrutinee_match_type = scrutinee_base;
  const bool has_else = static_cast<bool>(expr.else_expr);
  const bool requires_exhaustive = !has_else;

  const auto* union_type = std::get_if<TypeUnion>(&scrutinee_base->node);
    const auto* path_type = AppliedTypePath(*scrutinee_base);
    const ast::EnumDecl* enum_decl = nullptr;
    const ast::ModalDecl* modal_decl = nullptr;
    if (path_type) {
      enum_decl = LookupEnumDecl(ctx, *path_type);
      if (!enum_decl) {
        ast::TypePath ast_path;
        ast_path.reserve(path_type->size());
        for (const auto& comp : *path_type) {
          ast_path.push_back(comp);
        }
        modal_decl = LookupModalDecl(ctx, ast_path);
    }
  }

  std::vector<TypeRef> arm_types;
  for (const auto& arm : expr.cases) {
    if (!arm.pattern || !arm.body) {
      return result;
    }
    const auto case_scope = CaseScopeEnv(ctx,
                                         expr.scrutinee,
                                         env,
                                         arm.pattern,
                                         scrutinee_match_type,
                                         ast::Mutability::Let);
    if (!case_scope.ok) {
      result.diag_id = case_scope.diag_id;
      return result;
    }
    const TypeEnv arm_env = case_scope.env;
    const auto body = TypeArmBody(ctx, type_ctx, arm.body, arm_env);
    if (!body.ok) {
      result.diag_id = body.diag_id;
      return result;
    }
    arm_types.push_back(body.type);
  }

  if (has_else) {
    const auto else_typed = TypeExpr(ctx, type_ctx, expr.else_expr, env);
    if (!else_typed.ok) {
      result.diag_id = else_typed.diag_id;
      return result;
    }
    arm_types.push_back(else_typed.type);
  }

  const auto all_eq = AllEq(arm_types);
  if (!all_eq.ok) {
    result.diag_id = all_eq.diag_id;
    return result;
  }
  if (!all_eq.equal || arm_types.empty()) {
    SPEC_RULE("If-Branch-Mismatch");
    result.diag_id = "If-Branch-Mismatch";
    return result;
  }

  const auto unreachable = FindUnreachableArm(ctx, scrutinee_match_type, expr.cases);
  if (!unreachable.ok) {
    result.diag_id = unreachable.diag_id;
    return result;
  }
  if (unreachable.unreachable) {
    SPEC_RULE("IfCase-Unreachable");
    result.diag_id = "E-SEM-2751";
    return result;
  }

  if (enum_decl) {
    const auto arm_variants = ArmVariants(expr.cases, *path_type);
    const auto decl_variants = VariantNames(*enum_decl);
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        arm_variants != decl_variants) {
      SPEC_RULE("IfCase-Enum-NonExhaustive");
      result.diag_id = "E-SEM-2741";
      return result;
    }
    SPEC_RULE("T-IfCase-Enum");
  } else if (modal_decl) {
    const auto arm_states = ArmStates(ctx, expr.cases);
    const auto decl_states = StateNameSet(*modal_decl);
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        arm_states != decl_states) {
      SPEC_RULE("IfCase-Modal-NonExhaustive");
      result.diag_id = "E-TYP-2060";
      return result;
    }
    SPEC_RULE("T-IfCase-Modal");
  } else if (union_type) {
    const auto exhaustive = UnionTypesExhaustive(ctx, expr.cases,
                                                 union_type->members);
    if (!exhaustive.ok) {
      result.diag_id = exhaustive.diag_id;
      return result;
    }
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        !exhaustive.exhaustive) {
      SPEC_RULE("IfCase-Union-NonExhaustive");
      result.diag_id = "E-SEM-2705";
      return result;
    }
    SPEC_RULE("T-IfCase-Union");
  } else {
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type)) {
      SPEC_RULE("IfCase-Enum-NonExhaustive");
      result.diag_id = "E-SEM-2741";
      return result;
    }
    SPEC_RULE("T-IfCase-Other");
  }

  result.ok = true;
  result.type = arm_types.front();
  return result;
}

CheckResult CheckIfCaseExpr(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const ast::IfCaseExpr& expr,
                           const TypeEnv& env,
                           const TypeRef& expected) {
  SpecDefsIfCase();
  CheckResult result;
  if (!expr.scrutinee || !expected) {
    return result;
  }

  const auto scrutinee = TypeExpr(
      ctx, WithSharedAccessMode(type_ctx, ast::KeyMode::Read), expr.scrutinee,
      env);
  if (!scrutinee.ok) {
    result.diag_id = scrutinee.diag_id;
    return result;
  }

  const auto scrutinee_norm = NormalizeAliasTopLevel(ctx, StripPerm(scrutinee.type));
  if (!scrutinee_norm.ok) {
    result.diag_id = scrutinee_norm.diag_id;
    return result;
  }
  const TypeRef scrutinee_base = scrutinee_norm.type;
  if (!scrutinee_base) {
    return result;
  }
  const TypeRef scrutinee_match_type = scrutinee_base;
  const bool has_else = static_cast<bool>(expr.else_expr);
  const bool requires_exhaustive = !has_else;

  const auto* union_type = std::get_if<TypeUnion>(&scrutinee_base->node);
    const auto* path_type = AppliedTypePath(*scrutinee_base);
    const ast::EnumDecl* enum_decl = nullptr;
    const ast::ModalDecl* modal_decl = nullptr;
    if (path_type) {
      enum_decl = LookupEnumDecl(ctx, *path_type);
      if (!enum_decl) {
        ast::TypePath ast_path;
        ast_path.reserve(path_type->size());
        for (const auto& comp : *path_type) {
          ast_path.push_back(comp);
        }
        modal_decl = LookupModalDecl(ctx, ast_path);
    }
  }

  for (const auto& arm : expr.cases) {
    if (!arm.pattern || !arm.body) {
      return result;
    }
    const auto case_scope = CaseScopeEnv(ctx,
                                         expr.scrutinee,
                                         env,
                                         arm.pattern,
                                         scrutinee_match_type,
                                         ast::Mutability::Let);
    if (!case_scope.ok) {
      result.diag_id = case_scope.diag_id;
      return result;
    }
    const TypeEnv arm_env = case_scope.env;
    const auto check = CheckArmBody(ctx, type_ctx, arm.body, arm_env, expected);
    if (!check.ok) {
      result.diag_id = check.diag_id;
      return result;
    }
  }

  if (has_else) {
    const auto else_check =
        CheckExprAgainst(ctx, type_ctx, expr.else_expr, expected, env);
    if (!else_check.ok) {
      result.diag_id = else_check.diag_id;
      return result;
    }
  }

  const auto unreachable = FindUnreachableArm(ctx, scrutinee_match_type, expr.cases);
  if (!unreachable.ok) {
    result.diag_id = unreachable.diag_id;
    return result;
  }
  if (unreachable.unreachable) {
    SPEC_RULE("IfCase-Unreachable");
    result.diag_id = "E-SEM-2751";
    return result;
  }

  if (enum_decl) {
    const auto arm_variants = ArmVariants(expr.cases, *path_type);
    const auto decl_variants = VariantNames(*enum_decl);
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        arm_variants != decl_variants) {
      SPEC_RULE("IfCase-Enum-NonExhaustive");
      result.diag_id = "E-SEM-2741";
      return result;
    }
    SPEC_RULE("Chk-IfCase-Enum");
  } else if (modal_decl) {
    const auto arm_states = ArmStates(ctx, expr.cases);
    const auto decl_states = StateNameSet(*modal_decl);
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        arm_states != decl_states) {
      SPEC_RULE("IfCase-Modal-NonExhaustive");
      result.diag_id = "E-TYP-2060";
      return result;
    }
    SPEC_RULE("Chk-IfCase-Modal");
  } else if (union_type) {
    const auto exhaustive = UnionTypesExhaustive(ctx, expr.cases,
                                                 union_type->members);
    if (!exhaustive.ok) {
      result.diag_id = exhaustive.diag_id;
      return result;
    }
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type) &&
        !exhaustive.exhaustive) {
      SPEC_RULE("IfCase-Union-NonExhaustive");
      result.diag_id = "E-SEM-2705";
      return result;
    }
    SPEC_RULE("Chk-IfCase-Union");
  } else {
    if (requires_exhaustive &&
        !HasIrrefutableArm(ctx, expr.cases, scrutinee_match_type)) {
      SPEC_RULE("IfCase-Enum-NonExhaustive");
      result.diag_id = "E-SEM-2741";
      return result;
    }
    SPEC_RULE("Chk-IfCase-Other");
  }

  result.ok = true;
  return result;
}

}  // namespace cursive::analysis
