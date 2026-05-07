// =============================================================================
// MIGRATION MAPPING: layout_dispatch.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1 Layout and Representation (lines 14349-15153)
//   - LayoutJudg = {sizeof, alignof, layout} (line 14397)
//   - All layout rules from sections 6.1.1 through 6.1.8
//   - Permission layout pass-through (lines 14479-14492)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_dispatch.cpp
//   - SizeOf dispatch function
//   - AlignOf dispatch function
//   - LayoutOf dispatch function
//   - LowerTypeForLayout for type alias resolution
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (Layout struct)
//   - cursive/include/04_analysis/types/types.h (all type variants)
//   - All layout_*.cpp files for specific type handling
//
// REFACTORING NOTES:
//   1. Central dispatch for sizeof, alignof, layout queries
//   2. Type visitor pattern over TypeRef variants:
//      - TypePrim -> layout_primitives.cpp
//      - TypePerm -> unwrap permission, recurse
//      - TypePtr, TypeRawPtr -> PtrSize, PtrAlign
//      - TypeFunc -> PtrSize, PtrAlign (function pointer)
//      - TypePathType -> lookup record/enum/alias
//      - TypeTuple -> layout_tuples.cpp
//      - TypeArray -> element_size * length
//      - TypeSlice -> 2 * PtrSize
//      - TypeUnion -> layout_unions.cpp
//      - TypeString, TypeBytes -> layout by state
//      - TypeModalState -> layout_modal.cpp
//      - TypeDynamic -> layout_dynobj.cpp
//      - TypeRange* -> layout_aggregates.cpp
//   3. LowerTypeForLayout resolves type aliases
//   4. Returns std::optional to handle unknown/error types
//
// DISPATCH TABLE:
//   SizeOf(T) -> specific layout function based on T variant
//   AlignOf(T) -> specific layout function based on T variant
//   LayoutOf(T) -> {size, align} pair
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/builtin_modal_intrinsics.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_lookup.h"

namespace cursive::analysis::layout {
namespace {

struct LowerTypeCacheKey {
  const cursive::analysis::Sigma* sigma = nullptr;
  cursive::analysis::ExprTypeMap* expr_types = nullptr;
  const cursive::ast::ModulePath* module = nullptr;
  const cursive::ast::Type* type = nullptr;

  bool operator==(const LowerTypeCacheKey& other) const {
    return sigma == other.sigma && expr_types == other.expr_types &&
           module == other.module && type == other.type;
  }
};

struct LowerTypeCacheKeyHash {
  std::size_t operator()(const LowerTypeCacheKey& key) const noexcept {
    const auto h_sigma =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.sigma));
    const auto h_expr =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.expr_types));
    const auto h_module =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.module));
    const auto h_type =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.type));
    return static_cast<std::size_t>(
        (h_sigma >> 3) ^ (h_expr << 5) ^ (h_module >> 7) ^ (h_type << 11));
  }
};

using LowerTypeCacheMap = std::unordered_map<LowerTypeCacheKey,
                                             std::optional<cursive::analysis::TypeRef>,
                                             LowerTypeCacheKeyHash>;

LowerTypeCacheMap& LowerTypeCache() {
  static thread_local LowerTypeCacheMap cache;
  return cache;
}

void MaybeTrimLowerTypeCache(LowerTypeCacheMap& cache) {
  constexpr std::size_t kMaxEntries = 65536;
  if (cache.size() > kMaxEntries) {
    cache.clear();
  }
}

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align) {
  if (align == 0) {
    return value;
  }
  const std::uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

bool IsUnitType(const cursive::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  if (const auto* tup = std::get_if<cursive::analysis::TypeTuple>(&type->node)) {
    return tup->elements.empty();
  }
  return false;
}

bool IsNeverType(const cursive::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    return prim->name == "!";
  }
  return false;
}

bool IsRuntimeHandleModalPath(const cursive::analysis::TypePath& path) {
  return cursive::analysis::IsBuiltinRuntimeHandleModalTypePath(path);
}

std::vector<std::string>& ActiveLayoutQueries() {
  static thread_local std::vector<std::string> queries;
  return queries;
}

class ScopedLayoutQuery {
 public:
  explicit ScopedLayoutQuery(const cursive::analysis::TypeRef& type)
      : key_(cursive::analysis::TypeToString(type)) {
    auto& queries = ActiveLayoutQueries();
    recursive_ = std::find(queries.begin(), queries.end(), key_) != queries.end();
    if (!recursive_) {
      queries.push_back(key_);
    }
  }

  ScopedLayoutQuery(const ScopedLayoutQuery&) = delete;
  ScopedLayoutQuery& operator=(const ScopedLayoutQuery&) = delete;

  ~ScopedLayoutQuery() {
    if (!recursive_) {
      auto& queries = ActiveLayoutQueries();
      if (!queries.empty() && queries.back() == key_) {
        queries.pop_back();
      }
    }
  }

  bool recursive() const { return recursive_; }

 private:
  std::string key_;
  bool recursive_ = false;
};

std::string NormalizeAttrLiteral(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool ParseU64Literal(const cursive::ast::Token& tok, std::uint64_t& out) {
  if (tok.kind != cursive::lexer::TokenKind::IntLiteral) {
    return false;
  }
  std::string text = tok.lexeme;
  text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
  if (text.empty()) {
    return false;
  }
  std::size_t consumed = 0;
  try {
    const auto parsed = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) {
      return false;
    }
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

std::optional<std::uint64_t> ParseLayoutAlignArg(
    const cursive::ast::AttributeArg& arg) {
  if (!arg.key.has_value() || *arg.key != "align") {
    return std::nullopt;
  }
  const auto* nested = std::get_if<std::vector<cursive::ast::AttributeArg>>(&arg.value);
  if (!nested || nested->size() != 1) {
    return std::nullopt;
  }
  const auto* token = std::get_if<cursive::ast::Token>(&(*nested)[0].value);
  if (!token) {
    return std::nullopt;
  }
  std::uint64_t parsed = 0;
  if (!ParseU64Literal(*token, parsed)) {
    return std::nullopt;
  }
  return parsed;
}

bool IsEnumDiscTypeName(std::string_view name) {
  return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
         name == "u8" || name == "u16" || name == "u32" || name == "u64";
}

std::optional<Layout> AsyncLayoutFromArgs(const cursive::analysis::ScopeContext& ctx,
                                          const std::vector<cursive::analysis::TypeRef>& args) {
  std::uint64_t max_payload_size = 0;
  std::uint64_t max_payload_align = 1;

  auto add_payload_layout = [&](const std::optional<Layout>& layout_opt) {
    if (!layout_opt.has_value()) {
      return;
    }
    if (layout_opt->size == 0) {
      return;
    }
    max_payload_size = std::max(max_payload_size, layout_opt->size);
    max_payload_align = std::max(max_payload_align, layout_opt->align);
  };

  // Suspended payload is { output: Out, frame: Ptr<u8> } (frame is hidden impl detail)
  const auto out_type = args.size() > 0 ? args[0] : cursive::analysis::MakeTypePrim("()");
  const auto frame_ptr = cursive::analysis::MakeTypePtr(
      cursive::analysis::MakeTypePrim("u8"),
      cursive::analysis::PtrState::Valid);
  const auto suspended_layout = RecordLayoutOf(ctx, {out_type, frame_ptr});
  if (!suspended_layout.has_value()) {
    return std::nullopt;
  }
  add_payload_layout(suspended_layout->layout);

  // Completed payload is Result (if non-unit/never)
  if (args.size() > 2 && args[2] && !IsNeverType(args[2]) && !IsUnitType(args[2])) {
    add_payload_layout(LayoutOf(ctx, args[2]));
  }

  // Failed payload is E (if non-unit/never)
  if (args.size() > 3 && args[3] && !IsNeverType(args[3]) && !IsUnitType(args[3])) {
    add_payload_layout(LayoutOf(ctx, args[3]));
  }

  // Runtime async frame extraction assumes suspended payload stores a hidden
  // frame pointer at byte offset 8. Preserve this minimum payload contract
  // even when Out is zero-sized.
  constexpr std::uint64_t kAsyncFramePtrPayloadOffset = 8;
  const std::uint64_t ptr_size = PtrSize(ctx);
  const std::uint64_t ptr_align = PtrAlign(ctx);
  const std::uint64_t min_suspended_payload =
      kAsyncFramePtrPayloadOffset + ptr_size;
  max_payload_size = std::max(max_payload_size, min_suspended_payload);
  max_payload_align = std::max(max_payload_align, ptr_align);

  const std::uint64_t disc_size = 1;
  const std::uint64_t disc_align = 1;
  const std::uint64_t align = std::max(disc_align, max_payload_align);
  const std::uint64_t size = AlignUp(disc_size + max_payload_size, align);
  return Layout{size, align};
}

std::optional<Layout> AsyncLayoutFromSig(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::AsyncSig& sig) {
  std::vector<cursive::analysis::TypeRef> async_args;
  async_args.reserve(4);
  async_args.push_back(sig.out);
  async_args.push_back(sig.in);
  async_args.push_back(sig.result);
  async_args.push_back(sig.err);
  return AsyncLayoutFromArgs(ctx, async_args);
}

std::optional<std::string> DiscTypeName(std::uint64_t max_disc) {
  if (max_disc <= 0xFFu) {
    return std::string("u8");
  }
  if (max_disc <= 0xFFFFu) {
    return std::string("u16");
  }
  if (max_disc <= 0xFFFFFFFFu) {
    return std::string("u32");
  }
  return std::string("u64");
}

std::optional<Layout> DiscTypeLayout(std::uint64_t max_disc) {
  const auto name = DiscTypeName(max_disc);
  if (!name.has_value()) {
    return std::nullopt;
  }
  const auto size = PrimSize(*name);
  const auto align = PrimAlign(*name);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return Layout{*size, *align};
}

std::optional<cursive::analysis::Permission> LowerPermission(
    cursive::ast::TypePerm perm) {
  switch (perm) {
    case cursive::ast::TypePerm::Const:
      return cursive::analysis::Permission::Const;
    case cursive::ast::TypePerm::Unique:
      return cursive::analysis::Permission::Unique;
    case cursive::ast::TypePerm::Shared:
      return cursive::analysis::Permission::Shared;
  }
  return std::nullopt;
}

std::optional<cursive::analysis::ParamMode> LowerParamMode(
    const std::optional<cursive::ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  return cursive::analysis::ParamMode::Move;
}

cursive::analysis::RawPtrQual LowerRawPtrQual(cursive::ast::RawPtrQual qual) {
  return qual == cursive::ast::RawPtrQual::Imm
             ? cursive::analysis::RawPtrQual::Imm
             : cursive::analysis::RawPtrQual::Mut;
}

std::optional<cursive::analysis::StringState> LowerStringState(
    const std::optional<cursive::ast::StringState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  return *state == cursive::ast::StringState::Managed
             ? cursive::analysis::StringState::Managed
             : cursive::analysis::StringState::View;
}

std::optional<cursive::analysis::BytesState> LowerBytesState(
    const std::optional<cursive::ast::BytesState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  return *state == cursive::ast::BytesState::Managed
             ? cursive::analysis::BytesState::Managed
             : cursive::analysis::BytesState::View;
}

std::optional<cursive::analysis::PtrState> LowerPtrState(
    const std::optional<cursive::ast::PtrState>& state) {
  if (!state.has_value()) {
    return std::nullopt;
  }
  switch (*state) {
    case cursive::ast::PtrState::Valid:
      return cursive::analysis::PtrState::Valid;
    case cursive::ast::PtrState::Null:
      return cursive::analysis::PtrState::Null;
    case cursive::ast::PtrState::Expired:
      return cursive::analysis::PtrState::Expired;
  }
  return std::nullopt;
}

std::optional<Layout> ModalStringBytesLayout(
    const cursive::analysis::ScopeContext& ctx,
    std::uint64_t managed_size,
    std::uint64_t view_size) {
  const auto disc = DiscTypeLayout(1);
  if (!disc.has_value()) {
    return std::nullopt;
  }
  const std::uint64_t max_size = std::max(managed_size, view_size);
  const std::uint64_t align = std::max(disc->align, PtrAlign(ctx));
  const std::uint64_t size = AlignUp(disc->size + max_size, align);
  return Layout{size, align};
}

std::optional<cursive::analysis::TypeSubst> BuildDeclSubstitution(
    const std::optional<cursive::ast::GenericParams>& generic_params,
    const std::vector<cursive::analysis::TypeRef>& generic_args) {
  if (!generic_params.has_value() || generic_params->params.empty()) {
    return cursive::analysis::TypeSubst{};
  }
  if (generic_args.size() > generic_params->params.size()) {
    return std::nullopt;
  }
  return cursive::analysis::BuildSubstitution(generic_params->params, generic_args);
}

std::optional<cursive::analysis::TypeRef> LowerTypeForLayoutWithSubst(
    const cursive::analysis::ScopeContext& ctx,
    const std::shared_ptr<cursive::ast::Type>& type,
    const cursive::analysis::TypeSubst& subst) {
  const auto lowered = LowerTypeForLayout(ctx, type);
  if (!lowered.has_value()) {
    return std::nullopt;
  }
  cursive::analysis::TypeRef out = *lowered;
  if (!subst.empty()) {
    out = cursive::analysis::InstantiateType(out, subst);
  }
  return out;
}

}  // namespace

std::optional<LoweredAsyncType> LowerAsyncType(
    const cursive::analysis::AsyncSig& sig) {
  SPEC_RULE("Lower-Async-Type");

  std::vector<cursive::analysis::TypeRef> async_args;
  async_args.reserve(4);
  async_args.push_back(sig.out);
  async_args.push_back(sig.in);
  async_args.push_back(sig.result);
  async_args.push_back(sig.err);

  std::vector<std::string> states;
  states.reserve(3);
  states.push_back("Suspended");
  states.push_back("Completed");

  std::vector<cursive::analysis::TypeRef> members;
  members.reserve(3);
  members.push_back(cursive::analysis::MakeTypeModalState(
      {"Async"}, "Suspended", async_args));
  members.push_back(cursive::analysis::MakeTypeModalState(
      {"Async"}, "Completed", async_args));
  if (!IsNeverType(sig.err)) {
    states.push_back("Failed");
    members.push_back(cursive::analysis::MakeTypeModalState(
        {"Async"}, "Failed", std::move(async_args)));
  }

  return LoweredAsyncType{
      .states = std::move(states),
      .resume_type = cursive::analysis::MakeTypeUnion(std::move(members)),
  };
}

std::optional<LoweredAsyncType> LowerAsyncType(
    const cursive::analysis::TypeRef& type) {
  SPEC_RULE("Lower-Async-Type");
  const auto sig = cursive::analysis::GetAsyncSig(type);
  if (!sig.has_value()) {
    return std::nullopt;
  }
  return LowerAsyncType(*sig);
}

RecordLayoutOptions ResolveRecordLayoutOptions(
    const cursive::ast::AttributeList& attrs) {
  RecordLayoutOptions options{};
  for (const auto& attr : attrs) {
    if (attr.name == cursive::analysis::attrs::kLayout) {
      for (const auto& arg : attr.args) {
        if (const auto nested_align = ParseLayoutAlignArg(arg);
            nested_align.has_value()) {
          options.min_align = *nested_align;
          continue;
        }
        if (arg.key.has_value()) {
          continue;
        }
        const auto* token = std::get_if<cursive::ast::Token>(&arg.value);
        if (!token) {
          continue;
        }
        const auto value = NormalizeAttrLiteral(token->lexeme);
        if (value == "packed") {
          options.packed = true;
        }
      }
      continue;
    }
    if (attr.name == cursive::analysis::attrs::kAlign && !attr.args.empty()) {
      const auto* token = std::get_if<cursive::ast::Token>(&attr.args[0].value);
      std::uint64_t parsed = 0;
      if (token && ParseU64Literal(*token, parsed)) {
        options.min_align = parsed;
      }
    }
  }
  if (options.packed) {
    options.min_align = std::nullopt;
  }
  return options;
}

EnumLayoutOptions ResolveEnumLayoutOptions(
    const cursive::ast::AttributeList& attrs) {
  EnumLayoutOptions options{};
  for (const auto& attr : attrs) {
    if (attr.name == cursive::analysis::attrs::kLayout) {
      for (const auto& arg : attr.args) {
        if (const auto nested_align = ParseLayoutAlignArg(arg);
            nested_align.has_value()) {
          options.min_align = *nested_align;
          continue;
        }
        if (arg.key.has_value()) {
          continue;
        }
        const auto* token = std::get_if<cursive::ast::Token>(&arg.value);
        if (!token) {
          continue;
        }
        const auto value = NormalizeAttrLiteral(token->lexeme);
        if (IsEnumDiscTypeName(value)) {
          options.disc_type = value;
        }
      }
      continue;
    }
    if (attr.name == cursive::analysis::attrs::kAlign && !attr.args.empty()) {
      const auto* token = std::get_if<cursive::ast::Token>(&attr.args[0].value);
      std::uint64_t parsed = 0;
      if (token && ParseU64Literal(*token, parsed)) {
        options.min_align = parsed;
      }
    }
  }
  return options;
}

std::optional<cursive::analysis::TypeRef> LowerTypeForLayout(
    const cursive::analysis::ScopeContext& ctx,
    const std::shared_ptr<cursive::ast::Type>& type) {
  if (!type) {
    return std::nullopt;
  }
  const auto* sigma_key = ctx.sigma_source ? ctx.sigma_source : &ctx.sigma;
  const LowerTypeCacheKey cache_key{
      sigma_key, ctx.expr_types, &ctx.current_module, type.get()};
  auto& cache = LowerTypeCache();
  if (const auto it = cache.find(cache_key); it != cache.end()) {
    return it->second;
  }

  const auto lowered = std::visit(
      [&](const auto& node) -> std::optional<cursive::analysis::TypeRef> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, cursive::ast::TypePrim>) {
          return cursive::analysis::MakeTypePrim(node.name);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypePermType>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          const auto perm = LowerPermission(node.perm);
          if (!base.has_value() || !perm.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypePerm(*perm, *base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeUnion>) {
          std::vector<cursive::analysis::TypeRef> members;
          members.reserve(node.types.size());
          for (const auto& elem : node.types) {
            const auto lowered = LowerTypeForLayout(ctx, elem);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            members.push_back(*lowered);
          }
          return cursive::analysis::MakeTypeUnion(std::move(members));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeFunc>) {
          std::vector<cursive::analysis::TypeFuncParam> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerTypeForLayout(ctx, param.type);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            params.push_back(cursive::analysis::TypeFuncParam{
                LowerParamMode(param.mode), *lowered});
          }
          const auto ret = LowerTypeForLayout(ctx, node.ret);
          if (!ret.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeFunc(std::move(params), *ret);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeClosure>) {
          std::vector<std::pair<bool, cursive::analysis::TypeRef>> params;
          params.reserve(node.params.size());
          for (const auto& param : node.params) {
            const auto lowered = LowerTypeForLayout(ctx, param.type);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            params.emplace_back(param.mode.has_value(), *lowered);
          }
          const auto ret = LowerTypeForLayout(ctx, node.ret);
          if (!ret.has_value()) {
            return std::nullopt;
          }
          std::optional<std::vector<cursive::analysis::SharedDep>> deps_opt;
          if (node.deps_opt.has_value()) {
            std::vector<cursive::analysis::SharedDep> deps;
            deps.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              const auto lowered = LowerTypeForLayout(ctx, dep.type);
              if (!lowered.has_value()) {
                return std::nullopt;
              }
              deps.push_back(cursive::analysis::SharedDep{dep.name, *lowered});
            }
            deps_opt = std::move(deps);
          }
          return cursive::analysis::MakeTypeClosure(std::move(params), *ret, std::move(deps_opt));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeTuple>) {
          std::vector<cursive::analysis::TypeRef> elements;
          elements.reserve(node.elements.size());
          for (const auto& elem : node.elements) {
            const auto lowered = LowerTypeForLayout(ctx, elem);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            elements.push_back(*lowered);
          }
          return cursive::analysis::MakeTypeTuple(std::move(elements));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeArray>) {
          const auto elem = LowerTypeForLayout(ctx, node.element);
          if (!elem.has_value()) {
            return std::nullopt;
          }
          const auto len = cursive::analysis::ConstLen(ctx, node.length);
          if (!len.ok || !len.value.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeArray(*elem, *len.value);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeSlice>) {
          const auto elem = LowerTypeForLayout(ctx, node.element);
          if (!elem.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeSlice(*elem);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRange>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRange(*base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRangeInclusive>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRangeInclusive(*base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRangeFrom>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRangeFrom(*base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRangeTo>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRangeTo(*base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRangeToInclusive>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRangeToInclusive(*base);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRangeFull>) {
          return cursive::analysis::MakeTypeRangeFull();
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeSafePtr>) {
          const auto elem = LowerTypeForLayout(ctx, node.element);
          if (!elem.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypePtr(*elem, LowerPtrState(node.state));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRawPtr>) {
          const auto elem = LowerTypeForLayout(ctx, node.element);
          if (!elem.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRawPtr(LowerRawPtrQual(node.qual),
                                                *elem);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeString>) {
          return cursive::analysis::MakeTypeString(LowerStringState(node.state));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeBytes>) {
          return cursive::analysis::MakeTypeBytes(LowerBytesState(node.state));
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeDynamic>) {
          return cursive::analysis::MakeTypeDynamic(node.path);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeOpaque>) {
          return cursive::analysis::MakeTypeOpaque(node.path, type.get(),
                                                    type->span);
        } else if constexpr (std::is_same_v<T, cursive::ast::TypeRefine>) {
          const auto base = LowerTypeForLayout(ctx, node.base);
          if (!base.has_value()) {
            return std::nullopt;
          }
          return cursive::analysis::MakeTypeRefine(*base, node.predicate);
        } else if constexpr (std::is_same_v<T,
                                           cursive::ast::TypeModalState>) {
          std::vector<cursive::analysis::TypeRef> args;
          args.reserve(node.generic_args.size());
          for (const auto& arg : node.generic_args) {
            const auto lowered = LowerTypeForLayout(ctx, arg);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            args.push_back(*lowered);
          }
          return cursive::analysis::MakeTypeModalState(node.path, node.state,
                                                       std::move(args));
          } else if constexpr (std::is_same_v<T,
                                             cursive::ast::TypePathType>) {
          // Preserve generic arguments when lowering path types
          if (!node.generic_args.empty()) {
            std::vector<cursive::analysis::TypeRef> args;
            args.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              const auto lowered = LowerTypeForLayout(ctx, arg);
              if (!lowered.has_value()) {
                return std::nullopt;
              }
              args.push_back(*lowered);
            }
            return cursive::analysis::MakeTypePath(node.path, std::move(args));
            }
            return cursive::analysis::MakeTypePath(node.path);
          } else if constexpr (std::is_same_v<T, cursive::ast::TypeApply>) {
            std::vector<cursive::analysis::TypeRef> args;
            args.reserve(node.args.size());
            for (const auto& arg : node.args) {
              const auto lowered = LowerTypeForLayout(ctx, arg);
              if (!lowered.has_value()) {
                return std::nullopt;
              }
              args.push_back(*lowered);
            }
            return cursive::analysis::MakeTypeApply(node.path, std::move(args));
          } else {
            return std::nullopt;
          }
      },
      type->node);
  MaybeTrimLowerTypeCache(cache);
  cache.emplace(cache_key, lowered);
  return lowered;
}

std::optional<Layout> LayoutOf(const cursive::analysis::ScopeContext& ctx,
                               const cursive::analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  const std::uint64_t ptr_size = PtrSize(ctx);
  const std::uint64_t ptr_align = PtrAlign(ctx);

  const ScopedLayoutQuery query(type);
  if (query.recursive()) {
    return Layout{ptr_size, ptr_align};
  }

  if (const auto* perm = std::get_if<cursive::analysis::TypePerm>(&type->node)) {
    SPEC_RULE("Layout-Perm");
    return LayoutOf(ctx, perm->base);
  }
  if (const auto* refine =
          std::get_if<cursive::analysis::TypeRefine>(&type->node)) {
    return LayoutOf(ctx, refine->base);
  }
  if (const auto* opaque =
          std::get_if<cursive::analysis::TypeOpaque>(&type->node)) {
    if (opaque->origin) {
      const auto it = ctx.sigma.opaque_underlying.find(opaque->origin);
      if (it != ctx.sigma.opaque_underlying.end()) {
        return LayoutOf(ctx, it->second);
      }
    }
    return std::nullopt;
  }

  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    SPEC_RULE("Layout-Prim");
    const auto size = PrimSize(ctx, prim->name);
    const auto align = PrimAlign(ctx, prim->name);
    if (!size.has_value() || !align.has_value()) {
      return std::nullopt;
    }
    return Layout{*size, *align};
  }

  if (std::holds_alternative<cursive::analysis::TypePtr>(type->node)) {
    SPEC_RULE("Layout-Ptr");
    return Layout{ptr_size, ptr_align};
  }
  if (std::holds_alternative<cursive::analysis::TypeRawPtr>(type->node)) {
    SPEC_RULE("Layout-RawPtr");
    return Layout{ptr_size, ptr_align};
  }
  if (std::holds_alternative<cursive::analysis::TypeFunc>(type->node)) {
    SPEC_RULE("Layout-Func");
    return Layout{ptr_size, ptr_align};
  }
  if (std::holds_alternative<cursive::analysis::TypeClosure>(type->node)) {
    SPEC_RULE("Layout-Tuple");
    return Layout{ptr_size * 2, ptr_align};
  }

  if (const auto async_sig = analysis::GetAsyncSig(type)) {
    SPEC_RULE("Layout-Async");
    return AsyncLayoutFromSig(ctx, *async_sig);
  }

  if (std::holds_alternative<cursive::analysis::TypeDynamic>(type->node)) {
    const auto dyn = DynLayoutOf(ctx);
    SPEC_RULE("Layout-DynamicClass");
    return dyn.layout;
  }

  if (std::holds_alternative<cursive::analysis::TypeSlice>(type->node)) {
    SPEC_RULE("Layout-Slice");
    return Layout{2 * ptr_size, ptr_align};
  }

  if (const auto* str = std::get_if<cursive::analysis::TypeString>(&type->node)) {
    if (!str->state.has_value()) {
      const auto modal = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      return modal;
    }
    if (*str->state == cursive::analysis::StringState::Managed) {
      SPEC_RULE("Layout-String-Managed");
      return Layout{3 * ptr_size, ptr_align};
    }
    SPEC_RULE("Layout-String-View");
    return Layout{2 * ptr_size, ptr_align};
  }

  if (const auto* bytes =
          std::get_if<cursive::analysis::TypeBytes>(&type->node)) {
    if (!bytes->state.has_value()) {
      const auto modal = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      return modal;
    }
    if (*bytes->state == cursive::analysis::BytesState::Managed) {
      SPEC_RULE("Layout-Bytes-Managed");
      return Layout{3 * ptr_size, ptr_align};
    }
    SPEC_RULE("Layout-Bytes-View");
    return Layout{2 * ptr_size, ptr_align};
  }

  if (cursive::analysis::IsRangeType(type)) {
    SPEC_RULE("Layout-Range-SizeAlign");
    const auto layout = RangeLayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->layout;
  }

  if (const auto* array = std::get_if<cursive::analysis::TypeArray>(&type->node)) {
    SPEC_RULE("Layout-Array");
    const auto elem_layout = LayoutOf(ctx, array->element);
    if (!elem_layout.has_value()) {
      return std::nullopt;
    }
    const auto size = elem_layout->size * array->length;
    return Layout{size, elem_layout->align};
  }

  if (const auto* tuple = std::get_if<cursive::analysis::TypeTuple>(&type->node)) {
    if (tuple->elements.empty()) {
      SPEC_RULE("Layout-Tuple-Empty");
    } else {
      SPEC_RULE("Layout-Tuple-Cons");
    }
    const auto layout = TupleLayoutOf(ctx, tuple->elements);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    SPEC_RULE("Layout-Tuple");
    return layout->layout;
  }

  if (const auto* uni = std::get_if<cursive::analysis::TypeUnion>(&type->node)) {
    const auto layout = UnionLayoutOf(ctx, *uni);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    SPEC_RULE("Layout-Union");
    return layout->layout;
  }

  if (const auto* modal =
          std::get_if<cursive::analysis::TypeModalState>(&type->node)) {
    if (IsRuntimeHandleModalPath(modal->path)) {
      return Layout{ptr_size, ptr_align};
    }
    SPEC_RULE("Layout-ModalState");
    if (const auto builtin_layout =
            cursive::analysis::LookupBuiltinModalLayout(modal->path)) {
      return Layout{builtin_layout->size, builtin_layout->align};
    }
    const auto it = ctx.sigma.types.find(modal->path);
    if (it == ctx.sigma.types.end()) {
      return std::nullopt;
    }
    const auto* decl = std::get_if<cursive::ast::ModalDecl>(&it->second);
    if (!decl) {
      return std::nullopt;
    }
    const auto subst = BuildDeclSubstitution(decl->generic_params, modal->generic_args);
    if (!subst.has_value()) {
      return std::nullopt;
    }
    for (const auto& state : decl->states) {
      if (cursive::analysis::IdEq(state.name, modal->state)) {
        std::vector<cursive::analysis::TypeRef> fields;
        for (const auto& member : state.members) {
          if (const auto* field =
                  std::get_if<cursive::ast::StateFieldDecl>(&member)) {
            const auto lowered = LowerTypeForLayoutWithSubst(ctx, field->type, *subst);
            if (!lowered.has_value()) {
              return std::nullopt;
            }
            fields.push_back(*lowered);
          }
        }
        const auto layout = RecordLayoutOf(ctx, fields);
        if (!layout.has_value()) {
          return std::nullopt;
        }
        return layout->layout;
      }
    }
    return std::nullopt;
  }

    if (const auto* path = cursive::analysis::AppliedTypePath(*type)) {
      const auto* generic_args = cursive::analysis::AppliedTypeArgs(*type);
      const std::vector<cursive::analysis::TypeRef> empty_args;
      const auto& args = generic_args ? *generic_args : empty_args;
      if (IsRuntimeHandleModalPath(*path)) {
        return Layout{ptr_size, ptr_align};
      }
      if (const auto builtin_layout =
              cursive::analysis::LookupBuiltinModalLayout(*path)) {
        return Layout{builtin_layout->size, builtin_layout->align};
      }
      const auto* decl = cursive::analysis::LookupTypeDecl(ctx, *path);
      if (!decl) {
        return std::nullopt;
      }
      if (const auto* record = std::get_if<cursive::ast::RecordDecl>(decl)) {
        const auto subst = BuildDeclSubstitution(record->generic_params, args);
      if (!subst.has_value()) {
        return std::nullopt;
      }
      std::vector<cursive::analysis::TypeRef> fields;
      for (const auto& member : record->members) {
        if (const auto* field =
                std::get_if<cursive::ast::FieldDecl>(&member)) {
          const auto lowered = LowerTypeForLayoutWithSubst(ctx, field->type, *subst);
          if (!lowered.has_value()) {
            return std::nullopt;
          }
          fields.push_back(*lowered);
        }
      }
      const auto layout =
          RecordLayoutOf(ctx, fields, ResolveRecordLayoutOptions(record->attrs));
      if (!layout.has_value()) {
        return std::nullopt;
      }
      SPEC_RULE("Layout-Record");
      return layout->layout;
    }
    if (const auto* enum_decl = std::get_if<cursive::ast::EnumDecl>(decl)) {
      const auto layout =
          EnumLayoutOf(ctx, *enum_decl, args, ResolveEnumLayoutOptions(enum_decl->attrs));
      if (!layout.has_value()) {
        return std::nullopt;
      }
      SPEC_RULE("Layout-Enum");
      return layout->layout;
      }
      if (const auto* modal_decl = std::get_if<cursive::ast::ModalDecl>(decl)) {
        const auto layout = ModalLayoutOf(ctx, *modal_decl, args);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      SPEC_RULE("Layout-Modal");
      return layout->layout;
    }
      if (const auto* alias = std::get_if<cursive::ast::TypeAliasDecl>(decl)) {
      const auto lowered = LowerTypeForLayout(ctx, alias->type);
      if (!lowered.has_value()) {
        return std::nullopt;
      }
      cursive::analysis::TypeRef inst = *lowered;
        if (alias->generic_params &&
            !alias->generic_params->params.empty()) {
          SPEC_RULE("Layout-Alias");
          const auto subst = BuildDeclSubstitution(alias->generic_params, args);
        if (!subst.has_value()) {
          return std::nullopt;
        }
        inst = cursive::analysis::InstantiateType(inst, *subst);
      }
      SPEC_RULE("Layout-Alias");
      return LayoutOf(ctx, inst);
    }
  }

  return std::nullopt;
}

std::optional<std::uint64_t> SizeOf(const cursive::analysis::ScopeContext& ctx,
                                    const cursive::analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  const std::uint64_t ptr_size = PtrSize(ctx);
  if (const auto* perm = std::get_if<cursive::analysis::TypePerm>(&type->node)) {
    SPEC_RULE("Size-Perm");
    return SizeOf(ctx, perm->base);
  }
  if (const auto* refine =
          std::get_if<cursive::analysis::TypeRefine>(&type->node)) {
    return SizeOf(ctx, refine->base);
  }
  if (const auto* opaque =
          std::get_if<cursive::analysis::TypeOpaque>(&type->node)) {
    if (opaque->origin) {
      const auto it = ctx.sigma.opaque_underlying.find(opaque->origin);
      if (it != ctx.sigma.opaque_underlying.end()) {
        return SizeOf(ctx, it->second);
      }
    }
    return std::nullopt;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    SPEC_RULE("Size-Prim");
    return PrimSize(ctx, prim->name);
  }
  if (std::holds_alternative<cursive::analysis::TypePtr>(type->node)) {
    SPEC_RULE("Size-Ptr");
    return ptr_size;
  }
  if (std::holds_alternative<cursive::analysis::TypeRawPtr>(type->node)) {
    SPEC_RULE("Size-RawPtr");
    return ptr_size;
  }
  if (std::holds_alternative<cursive::analysis::TypeFunc>(type->node)) {
    SPEC_RULE("Size-Func");
    return ptr_size;
  }
  if (std::holds_alternative<cursive::analysis::TypeClosure>(type->node)) {
    SPEC_RULE("Size-Tuple");
    return 2 * ptr_size;
  }
  if (std::holds_alternative<cursive::analysis::TypeDynamic>(type->node)) {
    SPEC_RULE("Size-DynamicClass");
    return 2 * ptr_size;
  }
  if (std::holds_alternative<cursive::analysis::TypeSlice>(type->node)) {
    SPEC_RULE("Size-Slice");
    return 2 * ptr_size;
  }
  if (const auto async_sig = cursive::analysis::AsyncSigOf(ctx, type)) {
    SPEC_RULE("Size-Async");
    const auto layout = AsyncLayoutFromSig(ctx, *async_sig);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->size;
  }
  if (const auto* str = std::get_if<cursive::analysis::TypeString>(&type->node)) {
    if (!str->state.has_value()) {
      SPEC_RULE("Size-String-Modal");
      const auto layout = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->size;
    }
    if (*str->state == cursive::analysis::StringState::Managed) {
      SPEC_RULE("Size-String-Managed");
      return 3 * ptr_size;
    }
    SPEC_RULE("Size-String-View");
    return 2 * ptr_size;
  }
  if (const auto* bytes =
          std::get_if<cursive::analysis::TypeBytes>(&type->node)) {
    if (!bytes->state.has_value()) {
      SPEC_RULE("Size-Bytes-Modal");
      const auto layout = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->size;
    }
    if (*bytes->state == cursive::analysis::BytesState::Managed) {
      SPEC_RULE("Size-Bytes-Managed");
      return 3 * ptr_size;
    }
    SPEC_RULE("Size-Bytes-View");
    return 2 * ptr_size;
  }
  if (cursive::analysis::IsRangeType(type)) {
    SPEC_RULE("Size-Range");
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->size;
  }
  if (const auto* array = std::get_if<cursive::analysis::TypeArray>(&type->node)) {
    SPEC_RULE("Size-Array");
    const auto elem = SizeOf(ctx, array->element);
    if (!elem.has_value()) {
      return std::nullopt;
    }
    return (*elem) * array->length;
  }
  if (const auto* tuple = std::get_if<cursive::analysis::TypeTuple>(&type->node)) {
    (void)tuple;
    SPEC_RULE("Size-Tuple");
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->size;
  }
  if (const auto* uni = std::get_if<cursive::analysis::TypeUnion>(&type->node)) {
    (void)uni;
    SPEC_RULE("Size-Union");
    const auto layout = UnionLayoutOf(ctx, *uni);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->layout.size;
  }
  if (std::holds_alternative<cursive::analysis::TypeModalState>(type->node)) {
    SPEC_RULE("Size-ModalState");
    if (const auto* modal =
            std::get_if<cursive::analysis::TypeModalState>(&type->node);
        modal && IsRuntimeHandleModalPath(modal->path)) {
      return ptr_size;
    }
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->size;
  }
  if (const auto* path = cursive::analysis::AppliedTypePath(*type)) {
    const auto* generic_args = cursive::analysis::AppliedTypeArgs(*type);
    const std::vector<cursive::analysis::TypeRef> empty_args;
    const auto& args = generic_args ? *generic_args : empty_args;
    if (IsRuntimeHandleModalPath(*path)) {
      return ptr_size;
    }
    const auto* decl = cursive::analysis::LookupTypeDecl(ctx, *path);
    if (!decl) {
      return std::nullopt;
    }
    if (std::holds_alternative<cursive::ast::RecordDecl>(*decl)) {
      SPEC_RULE("Size-Record");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->size;
    }
    if (std::holds_alternative<cursive::ast::EnumDecl>(*decl)) {
      SPEC_RULE("Size-Enum");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->size;
    }
    if (std::holds_alternative<cursive::ast::ModalDecl>(*decl)) {
      SPEC_RULE("Size-Modal");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->size;
    }
    if (const auto* alias = std::get_if<cursive::ast::TypeAliasDecl>(decl)) {
      SPEC_RULE("Size-Alias");
      const auto lowered = LowerTypeForLayout(ctx, alias->type);
      if (!lowered.has_value()) {
        return std::nullopt;
      }
      cursive::analysis::TypeRef inst = *lowered;
      if (alias->generic_params &&
          !alias->generic_params->params.empty()) {
        const auto subst = BuildDeclSubstitution(alias->generic_params, args);
        if (!subst.has_value()) {
          return std::nullopt;
        }
        inst = cursive::analysis::InstantiateType(inst, *subst);
      }
      return SizeOf(ctx, inst);
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> AlignOf(const cursive::analysis::ScopeContext& ctx,
                                     const cursive::analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  const std::uint64_t ptr_size = PtrSize(ctx);
  const std::uint64_t ptr_align = PtrAlign(ctx);
  if (const auto* perm = std::get_if<cursive::analysis::TypePerm>(&type->node)) {
    SPEC_RULE("Align-Perm");
    return AlignOf(ctx, perm->base);
  }
  if (const auto* refine =
          std::get_if<cursive::analysis::TypeRefine>(&type->node)) {
    return AlignOf(ctx, refine->base);
  }
  if (const auto* opaque =
          std::get_if<cursive::analysis::TypeOpaque>(&type->node)) {
    if (opaque->origin) {
      const auto it = ctx.sigma.opaque_underlying.find(opaque->origin);
      if (it != ctx.sigma.opaque_underlying.end()) {
        return AlignOf(ctx, it->second);
      }
    }
    return std::nullopt;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    SPEC_RULE("Align-Prim");
    return PrimAlign(ctx, prim->name);
  }
  if (std::holds_alternative<cursive::analysis::TypePtr>(type->node)) {
    SPEC_RULE("Align-Ptr");
    return ptr_align;
  }
  if (std::holds_alternative<cursive::analysis::TypeRawPtr>(type->node)) {
    SPEC_RULE("Align-RawPtr");
    return ptr_align;
  }
  if (std::holds_alternative<cursive::analysis::TypeFunc>(type->node)) {
    SPEC_RULE("Align-Func");
    return ptr_align;
  }
  if (std::holds_alternative<cursive::analysis::TypeClosure>(type->node)) {
    SPEC_RULE("Align-Tuple");
    return ptr_align;
  }
  if (std::holds_alternative<cursive::analysis::TypeDynamic>(type->node)) {
    SPEC_RULE("Align-DynamicClass");
    return ptr_align;
  }
  if (std::holds_alternative<cursive::analysis::TypeSlice>(type->node)) {
    SPEC_RULE("Align-Slice");
    return ptr_align;
  }
  if (const auto async_sig = cursive::analysis::AsyncSigOf(ctx, type)) {
    SPEC_RULE("Align-Async");
    const auto layout = AsyncLayoutFromSig(ctx, *async_sig);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->align;
  }
  if (const auto* str = std::get_if<cursive::analysis::TypeString>(&type->node)) {
    if (!str->state.has_value()) {
      SPEC_RULE("Align-String-Modal");
      const auto layout = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->align;
    }
    if (*str->state == cursive::analysis::StringState::Managed) {
      SPEC_RULE("Align-String-Managed");
      return ptr_align;
    }
    SPEC_RULE("Align-String-View");
    return ptr_align;
  }
  if (const auto* bytes =
          std::get_if<cursive::analysis::TypeBytes>(&type->node)) {
    if (!bytes->state.has_value()) {
      SPEC_RULE("Align-Bytes-Modal");
      const auto layout = ModalStringBytesLayout(ctx, 3 * ptr_size, 2 * ptr_size);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->align;
    }
    if (*bytes->state == cursive::analysis::BytesState::Managed) {
      SPEC_RULE("Align-Bytes-Managed");
      return ptr_align;
    }
    SPEC_RULE("Align-Bytes-View");
    return ptr_align;
  }
  if (cursive::analysis::IsRangeType(type)) {
    SPEC_RULE("Align-Range");
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->align;
  }
  if (const auto* array = std::get_if<cursive::analysis::TypeArray>(&type->node)) {
    SPEC_RULE("Align-Array");
    return AlignOf(ctx, array->element);
  }
  if (const auto* tuple = std::get_if<cursive::analysis::TypeTuple>(&type->node)) {
    (void)tuple;
    SPEC_RULE("Align-Tuple");
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->align;
  }
  if (const auto* uni = std::get_if<cursive::analysis::TypeUnion>(&type->node)) {
    (void)uni;
    SPEC_RULE("Align-Union");
    const auto layout = UnionLayoutOf(ctx, *uni);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->layout.align;
  }
  if (std::holds_alternative<cursive::analysis::TypeModalState>(type->node)) {
    SPEC_RULE("Align-ModalState");
    if (const auto* modal =
            std::get_if<cursive::analysis::TypeModalState>(&type->node);
        modal && IsRuntimeHandleModalPath(modal->path)) {
      return ptr_align;
    }
    const auto layout = LayoutOf(ctx, type);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    return layout->align;
  }
  if (const auto* path = cursive::analysis::AppliedTypePath(*type)) {
    const auto* generic_args = cursive::analysis::AppliedTypeArgs(*type);
    const std::vector<cursive::analysis::TypeRef> empty_args;
    const auto& args = generic_args ? *generic_args : empty_args;
    if (IsRuntimeHandleModalPath(*path)) {
      return ptr_align;
    }
    const auto* decl = cursive::analysis::LookupTypeDecl(ctx, *path);
    if (!decl) {
      return std::nullopt;
    }
    if (std::holds_alternative<cursive::ast::RecordDecl>(*decl)) {
      SPEC_RULE("Align-Record");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->align;
    }
    if (std::holds_alternative<cursive::ast::EnumDecl>(*decl)) {
      SPEC_RULE("Align-Enum");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->align;
    }
    if (std::holds_alternative<cursive::ast::ModalDecl>(*decl)) {
      SPEC_RULE("Align-Modal");
      const auto layout = LayoutOf(ctx, type);
      if (!layout.has_value()) {
        return std::nullopt;
      }
      return layout->align;
    }
    if (const auto* alias = std::get_if<cursive::ast::TypeAliasDecl>(decl)) {
      SPEC_RULE("Align-Alias");
      const auto lowered = LowerTypeForLayout(ctx, alias->type);
      if (!lowered.has_value()) {
        return std::nullopt;
      }
      cursive::analysis::TypeRef inst = *lowered;
      if (alias->generic_params &&
          !alias->generic_params->params.empty()) {
        const auto subst = BuildDeclSubstitution(alias->generic_params, args);
        if (!subst.has_value()) {
          return std::nullopt;
        }
        inst = cursive::analysis::InstantiateType(inst, *subst);
      }
      return AlignOf(ctx, inst);
    }
  }
  return std::nullopt;
}

}  // namespace cursive::analysis::layout
