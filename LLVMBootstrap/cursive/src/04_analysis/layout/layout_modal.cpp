// =============================================================================
// MIGRATION MAPPING: layout_modal.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1.7 Modal Layout (Codegen) (lines 15086-15133)
//   - ModalDiscType definition (line 15088)
//   - StateSize, StateAlign (lines 15089-15090)
//   - ModalAlign, ModalSize (lines 15091-15092)
//   - Layout-Modal-Niche rule (lines 15095-15098)
//   - Layout-Modal-Tagged rule (lines 15100-15103)
//   - Size-Modal, Align-Modal, Layout-Modal rules (lines 15105-15118)
//   - Size-ModalState, Align-ModalState, Layout-ModalState rules (lines 15120-15133)
//   - Modal Niche Encoding (lines 14816-14835)
//   - ModalNicheBits, ModalTaggedBits, ModalBits definitions
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_modal.cpp
//   - ModalLayoutOf function (if exists)
//   - ModalPayload field access
//   - State enumeration helpers
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (ModalLayout struct)
//   - cursive/include/04_analysis/modal/modal_widen.h (modal type helpers)
//   - cursive/include/04_analysis/types/types.h (TypeModalState)
//   - RecordLayoutOf for state payload layout
//
// REFACTORING NOTES:
//   1. Modal types are tagged unions over states
//   2. Niche optimization applies when:
//      - One state has single-field payload with niches
//      - All other states are empty (no fields)
//      - Niche count >= number of states - 1
//   3. ModalDiscType = DiscType(|States(M)| - 1)
//   4. StateSize(S) = RecordLayout(ModalPayload(S)).size
//   5. ModalSize = AlignUp(disc_size + max_state_size, modal_align)
//   6. Modal state types (M@S) have size/align of just that state's payload
//   7. Full modal types (M) include discriminant overhead
//
// MODAL LAYOUT (TAGGED):
//   [discriminant][padding][payload: max_state_size][tail_padding]
//
// MODAL LAYOUT (NICHE):
//   Same as single payload state layout (no discriminant needed)
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>

#include "00_core/assert_spec.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis::layout {
namespace {

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

bool IsUnitType(const cursive::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  if (const auto* tuple = std::get_if<cursive::analysis::TypeTuple>(&type->node)) {
    return tuple->elements.empty();
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

std::optional<cursive::analysis::TypeSubst> BuildGenericSubstitution(
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

std::optional<RecordLayout> StatePayloadLayout(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::StateBlock& state,
    const cursive::analysis::TypeSubst& subst) {
  std::vector<cursive::analysis::TypeRef> fields;
  for (const auto& member : state.members) {
    if (const auto* field =
            std::get_if<cursive::ast::StateFieldDecl>(&member)) {
      const auto lowered = LowerTypeForLayout(ctx, field->type);
      if (!lowered.has_value()) {
        return std::nullopt;
      }
      cursive::analysis::TypeRef field_type = *lowered;
      if (!subst.empty()) {
        field_type = cursive::analysis::InstantiateType(field_type, subst);
      }
      fields.push_back(field_type);
    }
  }
  return RecordLayoutOf(ctx, fields);
}

}  // namespace

std::optional<ModalLayout> ModalLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::ModalDecl& decl,
    const std::vector<cursive::analysis::TypeRef>& generic_args) {
  if (decl.states.empty()) {
    return std::nullopt;
  }
  // Async has an implementation payload that includes a hidden frame pointer
  // in @Suspended. Its storage layout is not derivable from surface state
  // field declarations alone.
  if (cursive::analysis::IdEq(decl.name, "Async")) {
    cursive::analysis::TypeRef unit_type = cursive::analysis::MakeTypePrim("()");
    cursive::analysis::TypeRef never_type = cursive::analysis::MakeTypePrim("!");
    const cursive::analysis::TypeRef out_type =
        generic_args.size() > 0 ? generic_args[0] : unit_type;
    const cursive::analysis::TypeRef result_type =
        generic_args.size() > 2 ? generic_args[2] : unit_type;
    const cursive::analysis::TypeRef err_type =
        generic_args.size() > 3 ? generic_args[3] : never_type;

    const cursive::analysis::TypeRef frame_ptr =
        cursive::analysis::MakeTypePtr(
            cursive::analysis::MakeTypePrim("u8"),
            cursive::analysis::PtrState::Valid);
    const auto suspended_layout = RecordLayoutOf(ctx, {out_type, frame_ptr});
    if (!suspended_layout.has_value()) {
      return std::nullopt;
    }

    std::uint64_t payload_size = suspended_layout->layout.size;
    std::uint64_t payload_align = suspended_layout->layout.align;
    auto add_payload = [&](const cursive::analysis::TypeRef& payload_type) {
      const auto payload_layout = LayoutOf(ctx, payload_type);
      if (!payload_layout.has_value()) {
        return false;
      }
      if (payload_layout->size == 0) {
        return true;
      }
      payload_size = std::max(payload_size, payload_layout->size);
      payload_align = std::max(payload_align, payload_layout->align);
      return true;
    };

    if (!IsUnitType(result_type) && !IsNeverType(result_type)) {
      if (!add_payload(result_type)) {
        return std::nullopt;
      }
    }
    if (!IsUnitType(err_type) && !IsNeverType(err_type)) {
      if (!add_payload(err_type)) {
        return std::nullopt;
      }
    }

    // Runtime async frame extraction assumes suspended payload stores a hidden
    // frame pointer at byte offset 8 (see async runtime ABI). Ensure payload
    // capacity/alignment is sufficient even when Out is zero-sized.
    constexpr std::uint64_t kAsyncFramePtrPayloadOffset = 8;
    const std::uint64_t ptr_size = PtrSize(ctx);
    const std::uint64_t ptr_align = PtrAlign(ctx);
    const std::uint64_t min_suspended_payload =
        kAsyncFramePtrPayloadOffset + ptr_size;
    payload_size = std::max(payload_size, min_suspended_payload);
    payload_align = std::max(payload_align, ptr_align);

    const auto disc = DiscTypeLayout(decl.states.size() - 1);
    const auto disc_name = DiscTypeName(decl.states.size() - 1);
    if (!disc.has_value() || !disc_name.has_value()) {
      return std::nullopt;
    }
    const std::uint64_t align = std::max(disc->align, payload_align);
    const std::uint64_t size = AlignUp(disc->size + payload_size, align);

    ModalLayout out;
    out.niche = false;
    out.layout = Layout{size, align};
    out.payload_size = payload_size;
    out.payload_align = payload_align;
    out.disc_type = *disc_name;
    return out;
  }
  const auto subst = BuildGenericSubstitution(decl.generic_params, generic_args);
  if (!subst.has_value()) {
    return std::nullopt;
  }

  const auto payload_state = cursive::analysis::PayloadState(ctx, decl);
  if (payload_state.has_value()) {
    for (const auto& state : decl.states) {
      if (cursive::analysis::IdEq(state.name, *payload_state)) {
        const auto layout = StatePayloadLayout(ctx, state, *subst);
        if (!layout.has_value()) {
          return std::nullopt;
        }
        SPEC_RULE("Layout-Modal-Niche");
        ModalLayout out;
        out.niche = true;
        out.layout = layout->layout;
        out.niche_payload_layout = layout->layout;
        out.payload_size = layout->layout.size;
        out.payload_align = layout->layout.align;
        return out;
      }
    }
  }

  std::uint64_t max_size = 0;
  std::uint64_t max_align = 1;
  for (const auto& state : decl.states) {
    const auto layout = StatePayloadLayout(ctx, state, *subst);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    max_size = std::max(max_size, layout->layout.size);
    max_align = std::max(max_align, layout->layout.align);
  }

  const auto disc = DiscTypeLayout(decl.states.size() - 1);
  if (!disc.has_value()) {
    return std::nullopt;
  }
  const std::uint64_t align = std::max(disc->align, max_align);
  const std::uint64_t size = AlignUp(disc->size + max_size, align);

  SPEC_RULE("Layout-Modal-Tagged");
  ModalLayout out;
  out.niche = false;
  out.layout = Layout{size, align};
  out.payload_size = max_size;
  out.payload_align = max_align;
  out.disc_type = DiscTypeName(decl.states.size() - 1);
  return out;
}

}  // namespace cursive::analysis::layout
