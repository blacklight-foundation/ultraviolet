#include "04_analysis/typing/subtyping.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/contracts/verification.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/variance.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/composite/classes.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsSubtyping() {
  SPEC_DEF("SubtypingJudg", "5.2.2");
  SPEC_DEF("PermSubJudg", "5.2.2");
  SPEC_DEF("Member", "5.2.2");
  SPEC_DEF("Sub-Generic", "5.2.2");
  SPEC_DEF("Sub-Generic-Invariant-Err", "5.2.2");
  SPEC_DEF("Sub-Generic-Covariant-Err", "5.2.2");
  SPEC_DEF("Sub-Generic-Contravariant-Err", "5.2.2");
  SPEC_DEF("NicheCompatible", "5.7");
}

static constexpr std::array<std::string_view, 12> kIntTypes = {
    "i8",   "i16",  "i32",  "i64",  "i128", "u8",
    "u16",  "u32",  "u64",  "u128", "isize", "usize"};

static constexpr std::array<std::string_view, 3> kFloatTypes = {"f16",
                                                                "f32",
                                                                "f64"};

static bool IsIntType(std::string_view name) {
  for (const auto& t : kIntTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static bool IsFloatType(std::string_view name) {
  for (const auto& t : kFloatTypes) {
    if (name == t) {
      return true;
    }
  }
  return false;
}

static bool IsNumericMismatch(const TypeRef& lhs, const TypeRef& rhs) {
  const auto* lprim = std::get_if<TypePrim>(&lhs->node);
  const auto* rprim = std::get_if<TypePrim>(&rhs->node);
  if (!lprim || !rprim) {
    return false;
  }
  if (IsIntType(lprim->name) && IsIntType(rprim->name) &&
      lprim->name != rprim->name) {
    return true;
  }
  if (IsFloatType(lprim->name) && IsFloatType(rprim->name) &&
      lprim->name != rprim->name) {
    return true;
  }
  return false;
}

static bool IsNeverType(const TypeRef& type) {
  const auto* prim = std::get_if<TypePrim>(&type->node);
  return prim && prim->name == "!";
}

static bool SpanEq(const core::Span& lhs, const core::Span& rhs) {
  return lhs.file == rhs.file &&
         lhs.start_offset == rhs.start_offset &&
         lhs.end_offset == rhs.end_offset;
}

static bool PermEq(Permission lhs, Permission rhs) {
  SpecDefsSubtyping();
  if (lhs == Permission::Const && rhs == Permission::Const) {
    SPEC_RULE("Perm-Const");
    return true;
  }
  if (lhs == Permission::Unique && rhs == Permission::Unique) {
    SPEC_RULE("Perm-Unique");
    return true;
  }
  if (lhs == Permission::Shared && rhs == Permission::Shared) {
    SPEC_RULE("Perm-Shared");
    return true;
  }
  return false;
}

static bool TypeBaseCompatibleForArgument(const ScopeContext& ctx,
                                          const TypeRef& actual,
                                          const TypeRef& expected) {
  if (!actual || !expected) {
    return false;
  }
  const auto base_sub = Subtyping(ctx, actual, expected);
  return base_sub.ok && base_sub.subtype;
}

static bool TypePathEq(const TypePath& lhs, const TypePath& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (IdKeyOf(lhs[i]) != IdKeyOf(rhs[i])) {
      return false;
    }
  }
  return true;
}

struct AppliedTypeView {
  const TypePath* path = nullptr;
  const std::vector<TypeRef>* args = nullptr;
};

static std::optional<AppliedTypeView> GetAppliedTypeView(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
    if (const auto* path = std::get_if<TypePathType>(&type->node)) {
      return AppliedTypeView{&path->path, &path->generic_args};
    }
    if (const auto* apply = std::get_if<TypeApply>(&type->node)) {
      return AppliedTypeView{&apply->path, &apply->args};
    }
    return std::nullopt;
  }

static TypePathType ToTypePathType(const AppliedTypeView& view) {
  TypePathType out;
  out.path = *view.path;
  out.generic_args = *view.args;
  return out;
}

static const TypeDecl* LookupTypeDecl(
    const ScopeContext& ctx,
    const TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& segment : path) {
    syntax_path.push_back(segment);
  }
  const auto it = ctx.sigma.types.find(PathKeyOf(syntax_path));
  if (it != ctx.sigma.types.end()) {
    return &it->second;
  }

  // Single-segment names may need scope resolution to get the fully-qualified
  // module path for lookup in Σ.Types.
  if (path.size() != 1) {
    return nullptr;
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
  return &resolved_it->second;
}

static const ast::TypeAliasDecl* LookupTypeAliasDecl(
    const ScopeContext& ctx,
    const TypePath& path) {
  const auto* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return nullptr;
  }
  return std::get_if<ast::TypeAliasDecl>(decl);
}

struct GenericVarianceInfo {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool found = false;
  std::vector<ast::TypeParam> params;
  std::vector<TypeRef> member_types;
};

static void AppendLoweredType(GenericVarianceInfo& info,
                              const ScopeContext& ctx,
                              const std::shared_ptr<ast::Type>& type) {
  if (!info.ok || !type) {
    return;
  }
  const auto lowered = LowerType(ctx, type);
  if (!lowered.ok) {
    info.ok = false;
    info.diag_id = lowered.diag_id;
    return;
  }
  info.member_types.push_back(lowered.type);
}

static GenericVarianceInfo BuildGenericVarianceInfo(
    const ScopeContext& ctx,
    const TypePath& path) {
  GenericVarianceInfo info;
  const auto* decl = LookupTypeDecl(ctx, path);
  if (!decl) {
    return info;
  }
  info.found = true;

  std::visit(
      [&](const auto& node) {
        using D = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
          if (!node.generic_params.has_value()) {
            return;
          }
          info.params = node.generic_params->params;
          AppendLoweredType(info, ctx, node.type);
        } else if constexpr (std::is_same_v<D, ast::RecordDecl>) {
          if (!node.generic_params.has_value()) {
            return;
          }
          info.params = node.generic_params->params;
          for (const auto& member : node.members) {
            if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
              AppendLoweredType(info, ctx, field->type);
            }
          }
        } else if constexpr (std::is_same_v<D, ast::EnumDecl>) {
          if (!node.generic_params.has_value()) {
            return;
          }
          info.params = node.generic_params->params;
          for (const auto& variant : node.variants) {
            if (!variant.payload_opt.has_value()) {
              continue;
            }
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::VariantPayloadTuple>) {
                    for (const auto& elem : payload.elements) {
                      AppendLoweredType(info, ctx, elem);
                    }
                  } else if constexpr (std::is_same_v<P,
                                                      ast::VariantPayloadRecord>) {
                    for (const auto& field : payload.fields) {
                      AppendLoweredType(info, ctx, field.type);
                    }
                  }
                },
                *variant.payload_opt);
            if (!info.ok) {
              return;
            }
          }
        } else if constexpr (std::is_same_v<D, ast::ModalDecl>) {
          if (!node.generic_params.has_value()) {
            return;
          }
          info.params = node.generic_params->params;
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              if (const auto* field =
                      std::get_if<ast::StateFieldDecl>(&member)) {
                AppendLoweredType(info, ctx, field->type);
              }
            }
          }
        }
      },
      *decl);

  return info;
}

struct AliasExpandResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  TypeRef type = nullptr;
  bool expanded = false;
};

struct AliasNormalizeMemoKey {
  TypeRef type;

  bool operator==(const AliasNormalizeMemoKey& other) const {
    return type.get() == other.type.get();
  }
};

struct AliasNormalizeMemoKeyHash {
  std::size_t operator()(const AliasNormalizeMemoKey& key) const {
    return static_cast<std::size_t>(
        reinterpret_cast<std::uintptr_t>(key.type.get()) >> 4U);
  }
};

struct SubtypingMemoKey {
  TypeRef lhs;
  TypeRef rhs;

  bool operator==(const SubtypingMemoKey& other) const {
    return lhs.get() == other.lhs.get() && rhs.get() == other.rhs.get();
  }
};

struct SubtypingMemoKeyHash {
  std::size_t operator()(const SubtypingMemoKey& key) const {
    const auto lhs = reinterpret_cast<std::uintptr_t>(key.lhs.get());
    const auto rhs = reinterpret_cast<std::uintptr_t>(key.rhs.get());
    return static_cast<std::size_t>((lhs >> 4U) ^ (rhs << 3U) ^ rhs);
  }
};

struct SubtypingMemoState {
  int depth = 0;
  std::unordered_map<AliasNormalizeMemoKey,
                     AliasExpandResult,
                     AliasNormalizeMemoKeyHash>
      alias_normalize;
  std::unordered_map<SubtypingMemoKey, SubtypingResult, SubtypingMemoKeyHash>
      subtype;
};

SubtypingMemoState& CurrentSubtypingMemo() {
  thread_local SubtypingMemoState state;
  return state;
}

struct SubtypingMemoScope {
  SubtypingMemoScope() {
    auto& state = CurrentSubtypingMemo();
    if (state.depth == 0) {
      state.alias_normalize.clear();
      state.subtype.clear();
    }
    state.depth += 1;
  }

  ~SubtypingMemoScope() {
    auto& state = CurrentSubtypingMemo();
    state.depth -= 1;
    if (state.depth == 0) {
      state.alias_normalize.clear();
      state.subtype.clear();
    }
  }
};

static AliasExpandResult ExpandTypeAliasApply(
    const ScopeContext& ctx,
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

static AliasExpandResult NormalizeAliasDeepUncached(const ScopeContext& ctx,
                                                    const TypeRef& type,
                                                    int depth = 0);

static AliasExpandResult NormalizeAliasDeep(const ScopeContext& ctx,
                                            const TypeRef& type,
                                            int depth = 0) {
  if (!type || depth > 64) {
    return NormalizeAliasDeepUncached(ctx, type, depth);
  }

  auto& memo = CurrentSubtypingMemo().alias_normalize;
  const AliasNormalizeMemoKey key{type};
  if (const auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }

  AliasExpandResult result = NormalizeAliasDeepUncached(ctx, type, depth);
  memo.emplace(key, result);
  return result;
}

static AliasExpandResult NormalizeAliasDeepUncached(const ScopeContext& ctx,
                                                    const TypeRef& type,
                                                    int depth) {
  AliasExpandResult out;
  if (depth > 64) {
    out.type = type;
    return out;
  }

  const auto top = NormalizeAliasTopLevel(ctx, type);
  if (!top.ok) {
    return top;
  }
  TypeRef cur = top.type;
  bool changed = top.expanded;
  if (!cur) {
    out.type = cur;
    out.expanded = changed;
    return out;
  }

  if (const auto* perm = std::get_if<TypePerm>(&cur->node)) {
    const auto base = NormalizeAliasDeep(ctx, perm->base, depth + 1);
    if (!base.ok) {
      return base;
    }
    out.type = (changed || base.expanded)
                   ? MakeTypePerm(perm->perm, base.type)
                   : cur;
    out.expanded = changed || base.expanded;
    return out;
  }

  if (const auto* uni = std::get_if<TypeUnion>(&cur->node)) {
    bool member_changed = false;
    std::vector<TypeRef> members;
    members.reserve(uni->members.size());
    for (const auto& member : uni->members) {
      const auto normalized = NormalizeAliasDeep(ctx, member, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      member_changed = member_changed || normalized.expanded;
      members.push_back(normalized.type);
    }
    out.type = (changed || member_changed)
                   ? MakeTypeUnion(std::move(members))
                   : cur;
    out.expanded = changed || member_changed;
    return out;
  }

  if (const auto* func = std::get_if<TypeFunc>(&cur->node)) {
    bool param_changed = false;
    std::vector<TypeFuncParam> params;
    params.reserve(func->params.size());
    for (const auto& param : func->params) {
      const auto normalized = NormalizeAliasDeep(ctx, param.type, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      param_changed = param_changed || normalized.expanded;
      params.push_back(TypeFuncParam{param.mode, normalized.type});
    }
    const auto ret = NormalizeAliasDeep(ctx, func->ret, depth + 1);
    if (!ret.ok) {
      return ret;
    }
    out.type = (changed || param_changed || ret.expanded)
                   ? MakeTypeFunc(std::move(params), ret.type)
                   : cur;
    out.expanded = changed || param_changed || ret.expanded;
    return out;
  }

  if (const auto* tuple = std::get_if<TypeTuple>(&cur->node)) {
    bool elem_changed = false;
    std::vector<TypeRef> elems;
    elems.reserve(tuple->elements.size());
    for (const auto& elem : tuple->elements) {
      const auto normalized = NormalizeAliasDeep(ctx, elem, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      elem_changed = elem_changed || normalized.expanded;
      elems.push_back(normalized.type);
    }
    out.type = (changed || elem_changed)
                   ? MakeTypeTuple(std::move(elems))
                   : cur;
    out.expanded = changed || elem_changed;
    return out;
  }

  if (const auto* array = std::get_if<TypeArray>(&cur->node)) {
    const auto elem = NormalizeAliasDeep(ctx, array->element, depth + 1);
    if (!elem.ok) {
      return elem;
    }
    out.type = (changed || elem.expanded)
                   ? MakeTypeArray(elem.type, array->length)
                   : cur;
    out.expanded = changed || elem.expanded;
    return out;
  }

  if (const auto* slice = std::get_if<TypeSlice>(&cur->node)) {
    const auto elem = NormalizeAliasDeep(ctx, slice->element, depth + 1);
    if (!elem.ok) {
      return elem;
    }
    out.type = (changed || elem.expanded)
                   ? MakeTypeSlice(elem.type)
                   : cur;
    out.expanded = changed || elem.expanded;
    return out;
  }

  if (const auto* ptr = std::get_if<TypePtr>(&cur->node)) {
    const auto elem = NormalizeAliasDeep(ctx, ptr->element, depth + 1);
    if (!elem.ok) {
      return elem;
    }
    out.type = (changed || elem.expanded)
                   ? MakeTypePtr(elem.type, ptr->state)
                   : cur;
    out.expanded = changed || elem.expanded;
    return out;
  }

  if (const auto* raw = std::get_if<TypeRawPtr>(&cur->node)) {
    const auto elem = NormalizeAliasDeep(ctx, raw->element, depth + 1);
    if (!elem.ok) {
      return elem;
    }
    out.type = (changed || elem.expanded)
                   ? MakeTypeRawPtr(raw->qual, elem.type)
                   : cur;
    out.expanded = changed || elem.expanded;
    return out;
  }

  if (const auto* modal = std::get_if<TypeModalState>(&cur->node)) {
    bool arg_changed = false;
    std::vector<TypeRef> args;
    args.reserve(modal->generic_args.size());
    for (const auto& arg : modal->generic_args) {
      const auto normalized = NormalizeAliasDeep(ctx, arg, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      arg_changed = arg_changed || normalized.expanded;
      args.push_back(normalized.type);
    }
    out.type = (changed || arg_changed)
                   ? MakeTypeModalState(modal->path, modal->state, std::move(args))
                   : cur;
    out.expanded = changed || arg_changed;
    return out;
  }

    if (const auto* path = std::get_if<TypePathType>(&cur->node)) {
      bool arg_changed = false;
    std::vector<TypeRef> args;
    args.reserve(path->generic_args.size());
    for (const auto& arg : path->generic_args) {
      const auto normalized = NormalizeAliasDeep(ctx, arg, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      arg_changed = arg_changed || normalized.expanded;
      args.push_back(normalized.type);
    }
    out.type = (changed || arg_changed)
                   ? MakeTypePath(path->path, std::move(args))
                   : cur;
      out.expanded = changed || arg_changed;
      return out;
    }

    if (const auto* apply = std::get_if<TypeApply>(&cur->node)) {
      bool arg_changed = false;
      std::vector<TypeRef> args;
      args.reserve(apply->args.size());
      for (const auto& arg : apply->args) {
        const auto normalized = NormalizeAliasDeep(ctx, arg, depth + 1);
        if (!normalized.ok) {
          return normalized;
        }
        arg_changed = arg_changed || normalized.expanded;
        args.push_back(normalized.type);
      }
      out.type = (changed || arg_changed)
                     ? MakeTypeApply(apply->path, std::move(args))
                     : cur;
      out.expanded = changed || arg_changed;
      return out;
    }

  if (const auto* refine = std::get_if<TypeRefine>(&cur->node)) {
    const auto base = NormalizeAliasDeep(ctx, refine->base, depth + 1);
    if (!base.ok) {
      return base;
    }
    out.type = (changed || base.expanded)
                   ? MakeTypeRefine(base.type, refine->predicate)
                   : cur;
    out.expanded = changed || base.expanded;
    return out;
  }

  if (const auto* closure = std::get_if<TypeClosure>(&cur->node)) {
    bool param_changed = false;
    std::vector<std::pair<bool, TypeRef>> params;
    params.reserve(closure->params.size());
    for (const auto& param : closure->params) {
      const auto normalized = NormalizeAliasDeep(ctx, param.second, depth + 1);
      if (!normalized.ok) {
        return normalized;
      }
      param_changed = param_changed || normalized.expanded;
      params.push_back({param.first, normalized.type});
    }

    const auto ret = NormalizeAliasDeep(ctx, closure->ret, depth + 1);
    if (!ret.ok) {
      return ret;
    }

    bool deps_changed = false;
    std::optional<std::vector<SharedDep>> deps_opt;
    if (closure->deps_opt.has_value()) {
      std::vector<SharedDep> deps;
      deps.reserve(closure->deps_opt->size());
      for (const auto& dep : *closure->deps_opt) {
        const auto normalized = NormalizeAliasDeep(ctx, dep.type, depth + 1);
        if (!normalized.ok) {
          return normalized;
        }
        deps_changed = deps_changed || normalized.expanded;
        deps.push_back(SharedDep{dep.name, normalized.type});
      }
      deps_opt = std::move(deps);
    }

    out.type = (changed || param_changed || ret.expanded || deps_changed)
                   ? MakeTypeClosure(std::move(params), ret.type, deps_opt)
                   : cur;
    out.expanded = changed || param_changed || ret.expanded || deps_changed;
    return out;
  }

  out.type = cur;
  out.expanded = changed;
  return out;
}

static SubtypingResult Member(const ScopeContext& ctx,
                              const TypeRef& type,
                              const TypeUnion& uni) {
  SpecDefsSubtyping();
  for (const auto& member : uni.members) {
    const auto res = TypeEquiv(type, member);
    if (!res.ok) {
      return {false, res.diag_id, false};
    }
    if (res.equiv) {
      return {true, std::nullopt, true};
    }
    const auto forward = Subtyping(ctx, type, member);
    if (!forward.ok) {
      return {false, forward.diag_id, false};
    }
    if (!forward.subtype) {
      continue;
    }
    const auto reverse = Subtyping(ctx, member, type);
    if (!reverse.ok) {
      return {false, reverse.diag_id, false};
    }
    if (reverse.subtype) {
      return {true, std::nullopt, true};
    }
  }
  return {true, std::nullopt, false};
}

}  // namespace

static SubtypingResult SubtypingUncached(const ScopeContext& ctx,
                                         const TypeRef& lhs_in,
                                         const TypeRef& rhs_in) {
  SpecDefsSubtyping();
  const auto lhs_norm = NormalizeAliasDeep(ctx, lhs_in);
  if (!lhs_norm.ok) {
    return {false, lhs_norm.diag_id, false};
  }
  const auto rhs_norm = NormalizeAliasDeep(ctx, rhs_in);
  if (!rhs_norm.ok) {
    return {false, rhs_norm.diag_id, false};
  }

  const TypeRef lhs = lhs_norm.type;
  const TypeRef rhs = rhs_norm.type;
  if (!lhs || !rhs) {
    return {true, std::nullopt, false};
  }

  const auto equiv = TypeEquiv(lhs, rhs);
  if (!equiv.ok) {
    return {false, equiv.diag_id, false};
  }
  if (equiv.equiv) {
    return {true, std::nullopt, true};
  }

  if (const auto* lref = std::get_if<TypeRefine>(&lhs->node)) {
    if (const auto* rref = std::get_if<TypeRefine>(&rhs->node)) {
      const auto base_eq = TypeEquiv(lref->base, rref->base);
      if (!base_eq.ok) {
        return {false, base_eq.diag_id, false};
      }
      if (!base_eq.equiv) {
        return {true, std::nullopt, false};
      }
      SPEC_RULE("Sub-Refine");
      if (!lref->predicate || !rref->predicate) {
        return {true, std::nullopt, false};
      }
      StaticProofContext proof_ctx;
      AddFact(proof_ctx, lref->predicate, rref->predicate->span);
      const auto proof = StaticProof(proof_ctx, rref->predicate);
      if (proof.provable) {
        return {true, std::nullopt, true};
      }
      return {true, std::optional<std::string_view>{"E-TYP-1953"}, false};
    }
    SPEC_RULE("Sub-Refine-Elim");
    return Subtyping(ctx, lref->base, rhs);
  }
  if (std::holds_alternative<TypeRefine>(rhs->node)) {
    return {true, std::nullopt, false};
  }

  if (const auto* lopaque = std::get_if<TypeOpaque>(&lhs->node)) {
    const auto* ropaque = std::get_if<TypeOpaque>(&rhs->node);
    if (!ropaque) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-Opaque");
    if (SpanEq(lopaque->origin_span, ropaque->origin_span)) {
      return {true, std::nullopt, true};
    }
    return {true, std::optional<std::string_view>{"Opaque-Type-Mismatch"}, false};
  }
  if (std::holds_alternative<TypeOpaque>(rhs->node)) {
    return {true, std::nullopt, false};
  }

  if (IsNumericMismatch(lhs, rhs)) {
    return {true, std::nullopt, false};
  }

  if (IsNeverType(lhs)) {
    SPEC_RULE("Sub-Never");
    return {true, std::nullopt, true};
  }

  if (const auto lpath = GetAppliedTypeView(lhs)) {
    if (const auto rpath = GetAppliedTypeView(rhs)) {
      if (TypePathEq(*lpath->path, *rpath->path) &&
          lpath->args->size() == rpath->args->size()) {
        const auto variance_info = BuildGenericVarianceInfo(ctx, *lpath->path);
        if (!variance_info.ok) {
          return {false, variance_info.diag_id, false};
        }
        if (variance_info.found &&
            variance_info.params.size() == lpath->args->size()) {
          SPEC_RULE("Sub-Generic");
          for (std::size_t i = 0; i < lpath->args->size(); ++i) {
            const auto& lhs_arg = (*lpath->args)[i];
            const auto& rhs_arg = (*rpath->args)[i];
            const auto& param = variance_info.params[i];
            Variance variance = Variance::Invariant;
            if (!variance_info.member_types.empty()) {
              variance = Variance::Bivariant;
              for (const auto& member_type : variance_info.member_types) {
                variance = CombineVariance(
                    variance, VarianceOf(member_type, param.name));
              }
            }

            switch (variance) {
              case Variance::Covariant: {
                const auto sub = Subtyping(ctx, lhs_arg, rhs_arg);
                if (!sub.ok) {
                  return {false, sub.diag_id, false};
                }
                if (!sub.subtype) {
                  SPEC_RULE("Sub-Generic-Covariant-Err");
                  return {true, std::optional<std::string_view>{"E-TYP-1521"}, false};
                }
                break;
              }
              case Variance::Contravariant: {
                const auto sub = Subtyping(ctx, rhs_arg, lhs_arg);
                if (!sub.ok) {
                  return {false, sub.diag_id, false};
                }
                if (!sub.subtype) {
                  SPEC_RULE("Sub-Generic-Contravariant-Err");
                  return {true, std::optional<std::string_view>{"E-TYP-1521"}, false};
                }
                break;
              }
              case Variance::Invariant: {
                const auto equiv = TypeEquiv(lhs_arg, rhs_arg);
                if (!equiv.ok) {
                  return {false, equiv.diag_id, false};
                }
                if (!equiv.equiv) {
                  SPEC_RULE("Sub-Generic-Invariant-Err");
                  return {true, std::optional<std::string_view>{"E-TYP-1520"}, false};
                }
                break;
              }
              case Variance::Bivariant:
                break;
            }
          }
          return {true, std::nullopt, true};
        }
      }
    }
  }

  if (const auto lpath = GetAppliedTypeView(lhs)) {
    const auto expanded = ExpandTypeAliasApply(ctx, ToTypePathType(*lpath));
    if (!expanded.ok) {
      return {false, expanded.diag_id, false};
    }
    if (expanded.expanded) {
      return Subtyping(ctx, expanded.type, rhs);
    }
  }
  if (const auto rpath = GetAppliedTypeView(rhs)) {
    const auto expanded = ExpandTypeAliasApply(ctx, ToTypePathType(*rpath));
    if (!expanded.ok) {
      return {false, expanded.diag_id, false};
    }
    if (expanded.expanded) {
      return Subtyping(ctx, lhs, expanded.type);
    }
  }

  const auto lhs_async = AsyncSigOf(ctx, lhs);
  const auto rhs_async = AsyncSigOf(ctx, rhs);
  if (lhs_async.has_value() && rhs_async.has_value()) {
    const auto out_sub = Subtyping(ctx, lhs_async->out, rhs_async->out);
    if (!out_sub.ok) {
      return {false, out_sub.diag_id, false};
    }
    if (!out_sub.subtype) {
      return {true, std::nullopt, false};
    }
    const auto in_sub = Subtyping(ctx, rhs_async->in, lhs_async->in);
    if (!in_sub.ok) {
      return {false, in_sub.diag_id, false};
    }
    if (!in_sub.subtype) {
      return {true, std::nullopt, false};
    }
    const auto res_sub = Subtyping(ctx, lhs_async->result, rhs_async->result);
    if (!res_sub.ok) {
      return {false, res_sub.diag_id, false};
    }
    if (!res_sub.subtype) {
      return {true, std::nullopt, false};
    }
    const auto err_sub = Subtyping(ctx, lhs_async->err, rhs_async->err);
    if (!err_sub.ok) {
      return {false, err_sub.diag_id, false};
    }
    if (!err_sub.subtype) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-Async");
    return {true, std::nullopt, true};
  }

  if (const auto* ldyn = std::get_if<TypeDynamic>(&lhs->node)) {
    if (const auto* rdyn = std::get_if<TypeDynamic>(&rhs->node)) {
      if (IsExecutionDomainTypePath(rdyn->path)) {
        if (IsCpuDomainTypePath(ldyn->path) || IsGpuDomainTypePath(ldyn->path) ||
            IsInlineDomainTypePath(ldyn->path)) {
          return {true, std::nullopt, true};
        }
      }
    }
  }

  // §11.3: ConcreteType <: $ClassName when ConcreteType implements ClassName
  if (const auto* rdyn = std::get_if<TypeDynamic>(&rhs->node)) {
    if (TypeImplementsClass(ctx, lhs, rdyn->path)) {
      SPEC_RULE("Sub-Dynamic-Implements");
      return {true, std::nullopt, true};
    }
  }

  if (const auto* lstr = std::get_if<TypeString>(&lhs->node)) {
    if (const auto* rstr = std::get_if<TypeString>(&rhs->node)) {
      if (!rstr->state.has_value() && lstr->state.has_value()) {
        SPEC_RULE("Sub-String-Modal");
        return {true, std::nullopt, true};
      }
    }
  }

  if (const auto* lbytes = std::get_if<TypeBytes>(&lhs->node)) {
    if (const auto* rbytes = std::get_if<TypeBytes>(&rhs->node)) {
      if (!rbytes->state.has_value() && lbytes->state.has_value()) {
        SPEC_RULE("Sub-Bytes-Modal");
        return {true, std::nullopt, true};
      }
    }
  }

  const auto* lperm = std::get_if<TypePerm>(&lhs->node);
  const auto* rperm = std::get_if<TypePerm>(&rhs->node);
  if (lperm || rperm) {
    const auto lhs_perm = lperm ? lperm->perm : Permission::Const;
    const auto rhs_perm = rperm ? rperm->perm : Permission::Const;
    const auto lhs_base = lperm ? lperm->base : lhs;
    const auto rhs_base = rperm ? rperm->base : rhs;
    SPEC_RULE("Sub-Perm");
    if (!PermEq(lhs_perm, rhs_perm)) {
      return {true, std::nullopt, false};
    }
	    if (lhs_perm == Permission::Const && rhs_perm == Permission::Const) {
	      const auto* lpath = AppliedTypePath(*lhs_base);
	      const auto* largs = AppliedTypeArgs(*lhs_base);
	      const auto* rpath = AppliedTypePath(*rhs_base);
	      const auto* rargs = AppliedTypeArgs(*rhs_base);
	      if (lpath && largs && rpath && rargs && TypePathEq(*lpath, *rpath) &&
	          largs->size() == rargs->size()) {
	        SPEC_RULE("Sub-Perm-Const-Covariant");
	        for (std::size_t i = 0; i < largs->size(); ++i) {
	          const auto sub = Subtyping(ctx, (*largs)[i], (*rargs)[i]);
	          if (!sub.ok) {
	            return {false, sub.diag_id, false};
	          }
	          if (!sub.subtype) {
	            return {true, std::nullopt, false};
	          }
	        }
	        return {true, std::nullopt, true};
	      }
	    }
    return Subtyping(ctx, lhs_base, rhs_base);
  }

  if (const auto* lstr = std::get_if<TypeString>(&lhs->node)) {
    if (const auto* rstr = std::get_if<TypeString>(&rhs->node)) {
      if (!rstr->state.has_value() && lstr->state.has_value()) {
        SPEC_RULE("Sub-String-Widen");
        return {true, std::nullopt, true};
      }
    }
  }

  if (const auto* lbytes = std::get_if<TypeBytes>(&lhs->node)) {
    if (const auto* rbytes = std::get_if<TypeBytes>(&rhs->node)) {
      if (!rbytes->state.has_value() && lbytes->state.has_value()) {
        SPEC_RULE("Sub-Bytes-Widen");
        return {true, std::nullopt, true};
      }
    }
  }

  if (const auto* ltuple = std::get_if<TypeTuple>(&lhs->node)) {
    if (const auto* rtuple = std::get_if<TypeTuple>(&rhs->node)) {
      SPEC_RULE("Sub-Tuple");
      if (ltuple->elements.size() != rtuple->elements.size()) {
        return {true, std::nullopt, false};
      }
      for (std::size_t i = 0; i < ltuple->elements.size(); ++i) {
        const auto res = Subtyping(ctx, ltuple->elements[i],
                                   rtuple->elements[i]);
        if (!res.ok || !res.subtype) {
          return res.ok ? SubtypingResult{true, std::nullopt, false} : res;
        }
      }
      return {true, std::nullopt, true};
    }
  }

  if (const auto* larray = std::get_if<TypeArray>(&lhs->node)) {
    if (const auto* rslice = std::get_if<TypeSlice>(&rhs->node)) {
      SPEC_RULE("Coerce-Array-Slice");
      return Subtyping(ctx, larray->element, rslice->element);
    }
    if (const auto* rarray = std::get_if<TypeArray>(&rhs->node)) {
      SPEC_RULE("Sub-Array");
      if (larray->length != rarray->length) {
        return {true, std::nullopt, false};
      }
      return Subtyping(ctx, larray->element, rarray->element);
    }
  }

  if (const auto* lslice = std::get_if<TypeSlice>(&lhs->node)) {
    if (const auto* rslice = std::get_if<TypeSlice>(&rhs->node)) {
      SPEC_RULE("Sub-Slice");
      return Subtyping(ctx, lslice->element, rslice->element);
    }
  }

  if (const auto* lrange = std::get_if<TypeRange>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRange>(&rhs->node);
    if (!rrange) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-Range");
    return Subtyping(ctx, lrange->base, rrange->base);
  }

  if (const auto* lrange = std::get_if<TypeRangeInclusive>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeInclusive>(&rhs->node);
    if (!rrange) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-RangeInclusive");
    return Subtyping(ctx, lrange->base, rrange->base);
  }

  if (const auto* lrange = std::get_if<TypeRangeFrom>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeFrom>(&rhs->node);
    if (!rrange) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-RangeFrom");
    return Subtyping(ctx, lrange->base, rrange->base);
  }

  if (const auto* lrange = std::get_if<TypeRangeTo>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeTo>(&rhs->node);
    if (!rrange) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-RangeTo");
    return Subtyping(ctx, lrange->base, rrange->base);
  }

  if (const auto* lrange = std::get_if<TypeRangeToInclusive>(&lhs->node)) {
    const auto* rrange = std::get_if<TypeRangeToInclusive>(&rhs->node);
    if (!rrange) {
      return {true, std::nullopt, false};
    }
    SPEC_RULE("Sub-RangeToInclusive");
    return Subtyping(ctx, lrange->base, rrange->base);
  }

  if (std::holds_alternative<TypeRangeFull>(lhs->node) &&
      std::holds_alternative<TypeRangeFull>(rhs->node)) {
    SPEC_RULE("Sub-RangeFull");
    return {true, std::nullopt, true};
  }

  if (const auto* lptr = std::get_if<TypePtr>(&lhs->node)) {
    if (const auto* rptr = std::get_if<TypePtr>(&rhs->node)) {
      SPEC_RULE("Sub-Ptr-State");
      if (!rptr->state.has_value() && lptr->state.has_value()) {
        const auto state = *lptr->state;
        if (state == PtrState::Valid || state == PtrState::Null) {
          const auto elem_eq = TypeEquiv(lptr->element, rptr->element);
          if (!elem_eq.ok) {
            return {false, elem_eq.diag_id, false};
          }
          return {true, std::nullopt, elem_eq.equiv};
        }
      }
      return {true, std::nullopt, false};
    }
  }

  if (const auto* lmodal = std::get_if<TypeModalState>(&lhs->node)) {
    if (const auto* rmodal = std::get_if<TypeModalState>(&rhs->node)) {
      if (TypePathEq(lmodal->path, rmodal->path) &&
          !IdEq(lmodal->state, rmodal->state)) {
        SPEC_RULE("Modal-Incomparable");
        return {true, std::nullopt, false};
      }
      if (TypePathEq(lmodal->path, rmodal->path) &&
          lmodal->state == rmodal->state) {
        if (lmodal->generic_args.size() != rmodal->generic_args.size()) {
          return {true, std::nullopt, false};
        }
        for (std::size_t i = 0; i < lmodal->generic_args.size(); ++i) {
          const auto res = TypeEquiv(lmodal->generic_args[i], rmodal->generic_args[i]);
          if (!res.ok) {
            return {false, res.diag_id, false};
          }
          if (!res.equiv) {
            return {true, std::nullopt, false};
          }
        }
        SPEC_RULE("T-Equiv-ModalState");
        return {true, std::nullopt, true};
      }
    }
  }

  if (const auto* lmodal = std::get_if<TypeModalState>(&lhs->node)) {
    if (const auto rpath = GetAppliedTypeView(rhs)) {
      if (!TypePathEq(lmodal->path, *rpath->path)) {
        return {true, std::nullopt, false};
      }
      if (lmodal->generic_args.size() != rpath->args->size()) {
        return {true, std::nullopt, false};
      }
      for (std::size_t i = 0; i < lmodal->generic_args.size(); ++i) {
        const auto res = TypeEquiv(lmodal->generic_args[i], (*rpath->args)[i]);
        if (!res.ok) {
          return {false, res.diag_id, false};
        }
        if (!res.equiv) {
          return {true, std::nullopt, false};
        }
      }
      SPEC_RULE("Sub-Modal-Niche");
      if (!NicheCompatible(ctx, lmodal->path, lmodal->state)) {
        return {true, std::nullopt, false};
      }
      return {true, std::nullopt, true};
    }
  }

  if (const auto* lfunc = std::get_if<TypeFunc>(&lhs->node)) {
    if (const auto* rfunc = std::get_if<TypeFunc>(&rhs->node)) {
      SPEC_RULE("Sub-Func");
      if (lfunc->params.size() != rfunc->params.size()) {
        return {true, std::nullopt, false};
      }
      for (std::size_t i = 0; i < lfunc->params.size(); ++i) {
        const auto& lp = lfunc->params[i];
        const auto& rp = rfunc->params[i];
        if (lp.mode != rp.mode) {
          return {true, std::nullopt, false};
        }
        const auto res = Subtyping(ctx, rp.type, lp.type);
        if (!res.ok || !res.subtype) {
          return res.ok ? SubtypingResult{true, std::nullopt, false} : res;
        }
      }
      return Subtyping(ctx, lfunc->ret, rfunc->ret);
    }
  }

  if (const auto* lclosure = std::get_if<TypeClosure>(&lhs->node)) {
    if (const auto* rclosure = std::get_if<TypeClosure>(&rhs->node)) {
      SPEC_RULE("Sub-Closure");
      if (lclosure->params.size() != rclosure->params.size()) {
        return {true, std::nullopt, false};
      }
      for (std::size_t i = 0; i < lclosure->params.size(); ++i) {
        const auto& lp = lclosure->params[i];
        const auto& rp = rclosure->params[i];
        if (lp.first != rp.first) {
          return {true, std::nullopt, false};
        }
        const auto res = Subtyping(ctx, rp.second, lp.second);
        if (!res.ok || !res.subtype) {
          return res.ok ? SubtypingResult{true, std::nullopt, false} : res;
        }
      }

      if (lclosure->deps_opt.has_value() != rclosure->deps_opt.has_value()) {
        return {true, std::nullopt, false};
      }
      if (lclosure->deps_opt.has_value()) {
        if (lclosure->deps_opt->size() != rclosure->deps_opt->size()) {
          return {true, std::nullopt, false};
        }
        for (std::size_t i = 0; i < lclosure->deps_opt->size(); ++i) {
          const auto& ld = (*lclosure->deps_opt)[i];
          const auto& rd = (*rclosure->deps_opt)[i];
          if (ld.name != rd.name) {
            return {true, std::nullopt, false};
          }
          const auto dep_eq = TypeEquiv(ld.type, rd.type);
          if (!dep_eq.ok) {
            return {false, dep_eq.diag_id, false};
          }
          if (!dep_eq.equiv) {
            return {true, std::nullopt, false};
          }
        }
      }

      return Subtyping(ctx, lclosure->ret, rclosure->ret);
    }
  }

  if (const auto* lunion = std::get_if<TypeUnion>(&lhs->node)) {
    if (const auto* runion = std::get_if<TypeUnion>(&rhs->node)) {
      SPEC_RULE("Sub-Union-Width");
      for (const auto& member : lunion->members) {
        const auto res = Member(ctx, member, *runion);
        if (!res.ok || !res.subtype) {
          return res.ok ? SubtypingResult{true, std::nullopt, false} : res;
        }
      }
      return {true, std::nullopt, true};
    }
  }

  if (const auto* runion = std::get_if<TypeUnion>(&rhs->node)) {
    SPEC_RULE("Sub-Member-Union");
    return Member(ctx, lhs, *runion);
  }

  return {true, std::nullopt, false};
}

SubtypingResult Subtyping(const ScopeContext& ctx,
                          const TypeRef& lhs_in,
                          const TypeRef& rhs_in) {
  SubtypingMemoScope memo_scope;
  if (!lhs_in || !rhs_in) {
    return SubtypingUncached(ctx, lhs_in, rhs_in);
  }

  auto& memo = CurrentSubtypingMemo().subtype;
  const SubtypingMemoKey key{lhs_in, rhs_in};
  if (const auto it = memo.find(key); it != memo.end()) {
    return it->second;
  }

  SubtypingResult result = SubtypingUncached(ctx, lhs_in, rhs_in);
  memo.emplace(key, result);
  return result;
}

bool PermissionAdmits(Permission caller, Permission required) {
  switch (required) {
    case Permission::Const:
      return caller == Permission::Const ||
             caller == Permission::Shared ||
             caller == Permission::Unique;
    case Permission::Shared:
      return caller == Permission::Shared ||
             caller == Permission::Unique;
    case Permission::Unique:
      return caller == Permission::Unique;
  }
  return false;
}

SubtypingResult ArgumentTypeCompatible(const ScopeContext& ctx,
                                       const TypeRef& actual,
                                       const TypeRef& expected,
                                       const std::optional<ParamMode>& mode) {
  const auto sub = Subtyping(ctx, actual, expected);
  if (!sub.ok || sub.subtype || mode.has_value()) {
    return sub;
  }

  if (!actual || !expected) {
    return sub;
  }

  const auto* actual_perm_type = std::get_if<TypePerm>(&actual->node);
  const auto* expected_perm_type = std::get_if<TypePerm>(&expected->node);
  const Permission actual_perm = actual_perm_type ? actual_perm_type->perm
                                                 : Permission::Const;
  const Permission expected_perm = expected_perm_type ? expected_perm_type->perm
                                                     : Permission::Const;
  const TypeRef actual_base = actual_perm_type ? actual_perm_type->base
                                              : actual;
  const TypeRef expected_base = expected_perm_type ? expected_perm_type->base
                                                  : expected;

  if (!PermissionAdmits(actual_perm, expected_perm)) {
    return sub;
  }
  if (!TypeBaseCompatibleForArgument(ctx, actual_base, expected_base)) {
    return sub;
  }

  return {true, std::nullopt, true};
}

}  // namespace cursive::analysis
