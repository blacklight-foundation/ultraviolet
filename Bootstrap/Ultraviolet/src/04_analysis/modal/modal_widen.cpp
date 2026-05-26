/*
 * =============================================================================
 * modal_widen.cpp - Modal Widening Implementation
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - Docs/SPECIFICATION.md, Section 5.7 "Modal Widening" (line 12814)
 *   - Docs/SPECIFICATION.md, Section 5.7.1 "widen Expression" (lines 12820-12900)
 *   - Docs/SPECIFICATION.md, Section 7.2 "Modal Layout" (line 18619)
 *   - Docs/SPECIFICATION.md, Section 7.3 "Niche Optimization" (line 18635)
 *   - Docs/SPECIFICATION.md, Section 8.7 "E-MOD Errors" (lines 21600-21700)
 *
 * MIGRATED FROM:
 *   - ultraviolet-bootstrap/src/03_analysis/modal/modal_widen.cpp (lines 1-413)
 *
 * FUNCTIONS:
 *   - PayloadState(ctx, decl) -> optional<string_view>
 *       Find state with payload fields (for niche optimization)
 *   - NicheApplies(ctx, decl) -> bool
 *       Check if niche optimization can be applied to modal
 *   - NicheCompatible(ctx, modal_path, state) -> bool
 *       Check if type has a niche (unused bit patterns)
 *   - WidenWarnCond(ctx, modal_path, state) -> bool
 *       Check if widening warning should be emitted
 *   - WarnWidenLargePayload(ctx, type_ctx, span, modal_path, state) -> void
 *       Emit warning for large payload widening
 *
 * IMPLEMENTATION NOTES:
 *   1. `widen` converts Modal@State -> Modal (erases state information)
 *   2. After widening, state-specific fields/methods are inaccessible
 *   3. Niche optimization: Ptr<T>@Valid uses null as @Null discriminant
 *   4. Bool niche: values other than 0/1 can represent other states
 *   5. Char niche: invalid UTF-8 sequences can be used
 *   6. Consider warning when widening loses static state information
 *   7. Layout must account for discriminant tag when no niche available
 *
 * DIAGNOSTIC CODES:
 *   - W-SYS-4010: State information lost by widening (large payload)
 *
 * =============================================================================
 */

#include "04_analysis/modal/modal_widen.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/modal/modal.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_layout.h"

namespace ultraviolet::analysis {

namespace {

// =============================================================================
// Local type lowering utilities
// =============================================================================

// Local type lowering result structure (mirrors LowerTypeResult pattern)
struct LocalTypeLowerResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef type;
};

// SPEC_DEF: Permission lowering (Section 5.2.3)
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

// SPEC_DEF: Parameter mode lowering (Section 5.2.3)
static std::optional<ParamMode> LowerParamMode(
    const std::optional<ast::ParamMode>& mode) {
  if (!mode.has_value()) {
    return std::nullopt;
  }
  return ParamMode::Move;
}

// SPEC_DEF: Raw pointer qualifier lowering (Section 5.2.3)
static RawPtrQual LowerRawPtrQual(ast::RawPtrQual qual) {
  switch (qual) {
    case ast::RawPtrQual::Imm:
      return RawPtrQual::Imm;
    case ast::RawPtrQual::Mut:
      return RawPtrQual::Mut;
  }
  return RawPtrQual::Imm;
}

// SPEC_DEF: String state lowering (Section 5.2.3)
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

// SPEC_DEF: Bytes state lowering (Section 5.2.3)
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

// SPEC_DEF: Pointer state lowering (Section 5.2.3)
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

// Forward declaration for recursive lowering
static LocalTypeLowerResult LocalLowerType(
    const ScopeContext& ctx,
    const std::shared_ptr<ast::Type>& type);

// SPEC_DEF: Type lowering (Section 5.2.3)
// Local implementation of type lowering for modal widening analysis
static LocalTypeLowerResult LocalLowerType(
    const ScopeContext& ctx,
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
            params.push_back(
                TypeFuncParam{LowerParamMode(param.mode), lowered.type});
          }
          const auto ret = LocalLowerType(ctx, node.ret);
          if (!ret.ok) {
            return ret;
          }
          return {true, std::nullopt,
                  MakeTypeFunc(std::move(params), ret.type)};
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
          return {true, std::nullopt,
                  MakeTypeString(LowerStringState(node.state))};
        } else if constexpr (std::is_same_v<T, ast::TypeBytes>) {
          return {true, std::nullopt,
                  MakeTypeBytes(LowerBytesState(node.state))};
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
          // Section 5.2.9, Section 13.1: Generic type instantiation lowering
          // Per WF-Apply (Section 5.2.3), type arguments MUST be preserved
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

// =============================================================================
// State field counting utilities
// =============================================================================

// SPEC_DEF: StateFieldCount (Section 7.2)
// Count the number of fields in a modal state block
static std::size_t StateFieldCount(const ast::StateBlock& state) {
  std::size_t count = 0;
  for (const auto& member : state.members) {
    if (std::holds_alternative<ast::StateFieldDecl>(member)) {
      ++count;
    }
  }
  return count;
}

// SPEC_DEF: SingleFieldPayload (Section 7.3)
// If the state has exactly one field, return it; otherwise return nullptr
static const ast::StateFieldDecl* SingleFieldPayload(
    const ast::StateBlock& state) {
  const ast::StateFieldDecl* field = nullptr;
  for (const auto& member : state.members) {
    const auto* payload = std::get_if<ast::StateFieldDecl>(&member);
    if (!payload) {
      continue;
    }
    if (field) {
      // More than one field - not a single payload state
      return nullptr;
    }
    field = payload;
  }
  return field;
}

// SPEC_DEF: EmptyState (Section 7.3)
// Check if a state has no fields
static bool EmptyState(const ast::StateBlock& state) {
  return StateFieldCount(state) == 0;
}

// =============================================================================
// Niche counting
// =============================================================================

// SPEC_DEF: NicheCount (Section 7.3)
// Count available niches for a type. Currently only Ptr<T>@Valid has a niche.
// A niche is an unused bit pattern that can represent a discriminant.
//
// Ptr<T>@Valid: 1 niche (null pointer value)
// Bool: potentially 254 niches (non 0/1 values)
// Char: potentially many niches (invalid UTF-8)
//
// For simplicity, we currently only use the pointer niche.
static std::uint64_t NicheCount(const ScopeContext& ctx, const TypeRef& type) {
  (void)ctx;  // May be used for more sophisticated niche analysis
  if (!type) {
    return 0;
  }
  const auto* ptr = std::get_if<TypePtr>(&type->node);
  if (!ptr) {
    return 0;
  }
  // Only Ptr<T>@Valid has a niche (the null value)
  return ptr->state == PtrState::Valid ? 1 : 0;
}

}  // namespace

// =============================================================================
// Public API implementation
// =============================================================================

// SPEC_RULE: PayloadState (Section 7.3 Niche Optimization)
// Find the state with a single payload field that has niches.
// For niche optimization to apply:
// 1. Exactly one state must have a single field with niche count > 0
// 2. All other states must be empty (no fields)
// 3. The niche count must be >= number of other states
std::optional<std::string_view> PayloadState(const ScopeContext& ctx,
                                             const ast::ModalDecl& decl) {
  SPEC_RULE("PayloadState-Check");

  std::optional<std::string_view> candidate;
  std::uint64_t niche_count = 0;

  // Find a candidate state with a single niche-compatible field
  for (const auto& state : decl.states) {
    const auto* payload = SingleFieldPayload(state);
    if (!payload) {
      continue;
    }
    const auto lowered = LocalLowerType(ctx, payload->type);
    if (!lowered.ok) {
      return std::nullopt;
    }
    const auto count = NicheCount(ctx, lowered.type);
    if (count == 0) {
      continue;
    }
    // Multiple candidate states - niche optimization cannot apply
    if (candidate.has_value()) {
      SPEC_RULE("PayloadState-MultiplePayload");
      return std::nullopt;
    }
    candidate = state.name;
    niche_count = count;
  }

  if (!candidate.has_value()) {
    SPEC_RULE("PayloadState-NoCandidate");
    return std::nullopt;
  }

  // Verify all other states are empty
  for (const auto& state : decl.states) {
    if (IdEq(state.name, *candidate)) {
      continue;
    }
    if (!EmptyState(state)) {
      SPEC_RULE("PayloadState-NonEmptyOther");
      return std::nullopt;
    }
  }

  // Check niche count is sufficient for all other states
  const std::uint64_t required =
      decl.states.empty()
          ? 0ull
          : static_cast<std::uint64_t>(decl.states.size() - 1);
  if (niche_count < required) {
    SPEC_RULE("PayloadState-InsufficientNiches");
    return std::nullopt;
  }

  SPEC_RULE("PayloadState-Ok");
  return candidate;
}

// SPEC_RULE: NicheApplies (Section 7.3)
// Check if niche optimization can be applied to the modal type.
// This is true if PayloadState returns a valid state.
bool NicheApplies(const ScopeContext& ctx, const ast::ModalDecl& decl) {
  SPEC_RULE("NicheApplies-Check");
  return PayloadState(ctx, decl).has_value();
}

// SPEC_RULE: NicheCompatible (Section 7.3)
// Check if a specific modal state is compatible with niche optimization.
// Requirements:
// 1. The modal must support niche optimization
// 2. The given state must be the payload state
// 3. The state-specific type and modal type must have the same size and alignment
bool NicheCompatible(const ScopeContext& ctx,
                     const TypePath& modal_path,
                     std::string_view state) {
  SPEC_RULE("NicheCompatible-Check");

  const auto* decl = LookupModalDecl(ctx, modal_path);
  if (!decl) {
    SPEC_RULE("NicheCompatible-NoDecl");
    return false;
  }
  if (!HasState(*decl, state)) {
    SPEC_RULE("NicheCompatible-NoState");
    return false;
  }

  const auto payload_state = PayloadState(ctx, *decl);
  if (!payload_state.has_value() || !IdEq(*payload_state, state)) {
    SPEC_RULE("NicheCompatible-NotPayloadState");
    return false;
  }

  // Construct types for size/alignment comparison
  const auto state_type = MakeTypeModalState(modal_path, std::string(state));
  const auto modal_type = MakeTypePath(modal_path);

  // Compare sizes
  const auto state_size = SizeOf(ctx, state_type);
  const auto modal_size = SizeOf(ctx, modal_type);
  if (!state_size.has_value() || !modal_size.has_value()) {
    SPEC_RULE("NicheCompatible-SizeUnknown");
    return false;
  }
  if (*state_size != *modal_size) {
    SPEC_RULE("NicheCompatible-SizeMismatch");
    return false;
  }

  // Compare alignments
  const auto state_align = AlignOf(ctx, state_type);
  const auto modal_align = AlignOf(ctx, modal_type);
  if (!state_align.has_value() || !modal_align.has_value()) {
    SPEC_RULE("NicheCompatible-AlignUnknown");
    return false;
  }
  if (*state_align != *modal_align) {
    SPEC_RULE("NicheCompatible-AlignMismatch");
    return false;
  }

  SPEC_RULE("NicheCompatible-Ok");
  return true;
}

// SPEC_RULE: WidenWarnCond (Section 5.7.1)
// Determine if a warning should be emitted for widening.
// Warning is emitted when:
// 1. The state-specific type has a large payload (> threshold)
// 2. The niche optimization is NOT compatible
// This means the widened type will have a larger memory footprint.
bool WidenWarnCond(const ScopeContext& ctx,
                   const TypePath& modal_path,
                   std::string_view state) {
  SPEC_RULE("WidenWarnCond-Check");

  const auto* decl = LookupModalDecl(ctx, modal_path);
  if (!decl) {
    SPEC_RULE("WidenWarnCond-NoDecl");
    return false;
  }
  if (!HasState(*decl, state)) {
    SPEC_RULE("WidenWarnCond-NoState");
    return false;
  }

  // Check if state payload exceeds threshold
  const auto state_type = MakeTypeModalState(modal_path, std::string(state));
  const auto size = SizeOf(ctx, state_type);
  if (!size.has_value()) {
    SPEC_RULE("WidenWarnCond-SizeUnknown");
    return false;
  }
  if (*size <= kWidenLargePayloadThresholdBytes) {
    SPEC_RULE("WidenWarnCond-SmallPayload");
    return false;
  }

  // Only warn if niche optimization doesn't apply
  // (if niche applies, no memory penalty for widening)
  if (NicheCompatible(ctx, modal_path, state)) {
    SPEC_RULE("WidenWarnCond-NicheCompatible");
    return false;
  }

  SPEC_RULE("WidenWarnCond-Warn");
  return true;
}

// SPEC_RULE: WarnWidenLargePayload (Section 5.7.1)
// Emit a warning diagnostic when widening a large payload state.
// Warning code: W-SYS-4010
void WarnWidenLargePayload(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const core::Span& span,
                           const TypePath& modal_path,
                           std::string_view state) {
  SPEC_RULE("Warn-Widen-Check");

  if (!WidenWarnCond(ctx, modal_path, state)) {
    SPEC_RULE("Warn-Widen-Ok");
    return;
  }

  SPEC_RULE("Warn-Widen-LargePayload");

  if (!type_ctx.diags) {
    return;
  }

  if (auto diag = core::MakeDiagnosticById("W-SYS-4010", span)) {
    core::Emit(*type_ctx.diags, *diag);
  }
}

}  // namespace ultraviolet::analysis
