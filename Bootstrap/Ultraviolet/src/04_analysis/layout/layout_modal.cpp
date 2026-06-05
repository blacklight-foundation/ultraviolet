// =============================================================================
// MIGRATION MAPPING: layout_modal.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
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
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/layout/layout_modal.cpp
//   - ModalLayoutOf function (if exists)
//   - ModalPayload field access
//   - State enumeration helpers
//
// DEPENDENCIES:
//   - ultraviolet/include/04_analysis/layout/layout.h (ModalLayout struct)
//   - ultraviolet/include/04_analysis/modal/modal_widen.h (modal type helpers)
//   - ultraviolet/include/04_analysis/types/types.h (TypeModalState)
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
#include <string>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/spec_trace.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis::layout {
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

bool IsUnitType(const ultraviolet::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  if (const auto* tuple = std::get_if<ultraviolet::analysis::TypeTuple>(&type->node)) {
    return tuple->elements.empty();
  }
  return false;
}

bool IsNeverType(const ultraviolet::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<ultraviolet::analysis::TypePrim>(&type->node)) {
    return prim->name == "!";
  }
  return false;
}

std::optional<ultraviolet::analysis::TypeSubst> BuildGenericSubstitution(
    const std::optional<ultraviolet::ast::GenericParams>& generic_params,
    const std::vector<ultraviolet::analysis::TypeRef>& generic_args) {
  if (!generic_params.has_value() || generic_params->params.empty()) {
    return ultraviolet::analysis::TypeSubst{};
  }
  if (generic_args.size() > generic_params->params.size()) {
    return std::nullopt;
  }
  return ultraviolet::analysis::BuildSubstitution(generic_params->params, generic_args);
}

std::optional<RecordLayout> StatePayloadLayout(
    const ultraviolet::analysis::ScopeContext& ctx,
    const ultraviolet::ast::StateBlock& state,
    const ultraviolet::analysis::TypeSubst& subst) {
  std::vector<ultraviolet::analysis::TypeRef> fields;
  for (const auto& member : state.members) {
    if (const auto* field =
            std::get_if<ultraviolet::ast::StateFieldDecl>(&member)) {
      const auto lowered = LowerTypeForLayout(ctx, field->type);
      if (!lowered.has_value()) {
        return std::nullopt;
      }
      ultraviolet::analysis::TypeRef field_type = *lowered;
      if (!subst.empty()) {
        field_type = ultraviolet::analysis::InstantiateType(field_type, subst);
      }
      fields.push_back(field_type);
    }
  }
  return RecordLayoutOf(ctx, fields);
}

std::size_t ModalStateFieldCount(const ultraviolet::ast::StateBlock& state) {
  std::size_t count = 0;
  for (const auto& member : state.members) {
    if (std::holds_alternative<ultraviolet::ast::StateFieldDecl>(member)) {
      ++count;
    }
  }
  return count;
}

struct ModalStateLayoutFact {
  std::string name;
  std::size_t field_count = 0;
  Layout layout;
};

std::optional<std::vector<ModalStateLayoutFact>> ModalStateLayoutFacts(
    const ultraviolet::analysis::ScopeContext& ctx,
    const ultraviolet::ast::ModalDecl& decl,
    const ultraviolet::analysis::TypeSubst& subst) {
  std::vector<ModalStateLayoutFact> facts;
  facts.reserve(decl.states.size());
  for (const auto& state : decl.states) {
    const auto layout = StatePayloadLayout(ctx, state, subst);
    if (!layout.has_value()) {
      return std::nullopt;
    }
    ModalStateLayoutFact fact;
    fact.name = state.name;
    fact.field_count = ModalStateFieldCount(state);
    fact.layout = layout->layout;
    facts.push_back(std::move(fact));
  }
  return facts;
}

std::string ModalLayoutFactsPayload(
    const ultraviolet::ast::ModalDecl& decl,
    const std::vector<ModalStateLayoutFact>& facts,
    const ModalLayout& layout,
    std::string_view kind,
    const std::optional<std::string_view>& payload_state) {
  std::string payload = "source=ModalLayoutOf";
  payload += ";modal=";
  payload += decl.name;
  payload += ";kind=";
  payload += kind;
  payload += ";state_count=";
  payload += std::to_string(facts.size());

  std::string empty_states;
  std::string single_field_states;
  for (const auto& fact : facts) {
    payload += ";state_";
    payload += fact.name;
    payload += "=size";
    payload += std::to_string(fact.layout.size);
    payload += "_align";
    payload += std::to_string(fact.layout.align);
    payload += "_fields";
    payload += std::to_string(fact.field_count);

    if (fact.field_count == 0) {
      if (!empty_states.empty()) {
        empty_states += ",";
      }
      empty_states += fact.name;
    } else if (fact.field_count == 1) {
      if (!single_field_states.empty()) {
        single_field_states += ",";
      }
      single_field_states += fact.name;
    }
  }

  payload += ";empty_states=";
  payload += empty_states.empty() ? "none" : empty_states;
  payload += ";single_field_states=";
  payload += single_field_states.empty() ? "none" : single_field_states;
  payload += ";payload_state=";
  payload += payload_state.has_value() ? std::string(*payload_state) : "none";
  payload += ";disc_type=";
  payload += layout.disc_type.has_value() ? *layout.disc_type : "none";
  payload += ";disc_size=";
  if (layout.disc_type.has_value()) {
    const auto disc_size = PrimSize(*layout.disc_type);
    payload += std::to_string(disc_size.value_or(0));
  } else {
    payload += "0";
  }
  payload += ";payload_offset=";
  if (layout.disc_type.has_value()) {
    const auto disc_size = PrimSize(*layout.disc_type);
    payload += std::to_string(
        AlignUp(disc_size.value_or(0), layout.payload_align));
  } else {
    payload += "0";
  }
  payload += ";size=";
  payload += std::to_string(layout.layout.size);
  payload += ";align=";
  payload += std::to_string(layout.layout.align);
  payload += ";payload_size=";
  payload += std::to_string(layout.payload_size);
  payload += ";payload_align=";
  payload += std::to_string(layout.payload_align);
  if (layout.niche_payload_layout.has_value()) {
    payload += ";niche_payload_size=";
    payload += std::to_string(layout.niche_payload_layout->size);
    payload += ";niche_payload_align=";
    payload += std::to_string(layout.niche_payload_layout->align);
  }
  return payload;
}

void RecordModalLayoutFacts(
    const ultraviolet::ast::ModalDecl& decl,
    const std::vector<ModalStateLayoutFact>& facts,
    const ModalLayout& layout,
    std::string_view kind,
    const std::optional<std::string_view>& payload_state) {
  if (!ultraviolet::core::Conformance::Enabled()) {
    return;
  }

  const std::string payload =
      ModalLayoutFactsPayload(decl, facts, layout, kind, payload_state);
  auto record = [&](std::string_view obligation) {
    ultraviolet::core::Conformance::Record(obligation, std::nullopt, payload);
  };

  record("def.ModalStateLayoutMetrics");
  record("def.ModalSingleFieldPayload");
  record("def.ModalEmptyState");
  record("def.ModalEmptyStates");
  record("def.ModalAlign");
  record("def.ModalSize");
  record("def.ModalPayloadSize");
  record("def.ModalPayloadAlign");
  record("def.ModalLayoutJudgementSet");
  record("rule.13.Size-Modal");
  record("rule.13.Align-Modal");
  record("rule.13.Layout-Modal");
  record("rule.13.Size-ModalState");
  record("rule.13.Align-ModalState");
  record("rule.13.Layout-ModalState");
  record("def.ModalStateLayoutEquation");
  record("def.EmptyModalStateSizeEquation");
  record("def.ModalBaseLayoutEquation");

  if (layout.disc_type.has_value()) {
    record("def.ModalDiscType");
  }
  if (payload_state.has_value()) {
    record("def.ModalPayloadState");
    record("def.ModalNicheApplies");
  }
}

}  // namespace

std::optional<ModalLayout> ModalLayoutOf(
    const ultraviolet::analysis::ScopeContext& ctx,
    const ultraviolet::ast::ModalDecl& decl,
    const std::vector<ultraviolet::analysis::TypeRef>& generic_args) {
  if (decl.states.empty()) {
    return std::nullopt;
  }
  // Async has an implementation payload that includes a hidden frame pointer
  // in @Suspended. Its storage layout is not derivable from surface state
  // field declarations alone.
  if (ultraviolet::analysis::IdEq(decl.name, "Async")) {
    ultraviolet::analysis::TypeRef unit_type = ultraviolet::analysis::MakeTypePrim("()");
    ultraviolet::analysis::TypeRef never_type = ultraviolet::analysis::MakeTypePrim("!");
    const ultraviolet::analysis::TypeRef out_type =
        generic_args.size() > 0 ? generic_args[0] : unit_type;
    const ultraviolet::analysis::TypeRef result_type =
        generic_args.size() > 2 ? generic_args[2] : unit_type;
    const ultraviolet::analysis::TypeRef err_type =
        generic_args.size() > 3 ? generic_args[3] : never_type;

    const ultraviolet::analysis::TypeRef frame_ptr =
        ultraviolet::analysis::MakeTypePtr(
            ultraviolet::analysis::MakeTypePrim("u8"),
            ultraviolet::analysis::PtrState::Valid);
    const auto suspended_layout = RecordLayoutOf(ctx, {out_type, frame_ptr});
    if (!suspended_layout.has_value()) {
      return std::nullopt;
    }

    std::uint64_t payload_size = suspended_layout->layout.size;
    std::uint64_t payload_align = suspended_layout->layout.align;
    auto add_payload = [&](const ultraviolet::analysis::TypeRef& payload_type) {
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
  const auto state_layouts = ModalStateLayoutFacts(ctx, decl, *subst);
  if (!state_layouts.has_value()) {
    return std::nullopt;
  }

  const auto payload_state = ultraviolet::analysis::PayloadState(ctx, decl);
  if (payload_state.has_value()) {
    for (const auto& fact : *state_layouts) {
      if (ultraviolet::analysis::IdEq(fact.name, *payload_state)) {
        SPEC_RULE("Layout-Modal-Niche");
        ModalLayout out;
        out.niche = true;
        out.layout = fact.layout;
        out.niche_payload_layout = fact.layout;
        out.payload_size = fact.layout.size;
        out.payload_align = fact.layout.align;
        RecordModalLayoutFacts(decl, *state_layouts, out, "niche", payload_state);
        return out;
      }
    }
  }

  std::uint64_t max_size = 0;
  std::uint64_t max_align = 1;
  for (const auto& fact : *state_layouts) {
    max_size = std::max(max_size, fact.layout.size);
    max_align = std::max(max_align, fact.layout.align);
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
  RecordModalLayoutFacts(decl, *state_layouts, out, "tagged", std::nullopt);
  return out;
}

}  // namespace ultraviolet::analysis::layout
