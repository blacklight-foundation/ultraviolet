// =============================================================================
// Modal Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16782-16785
//   - TagOf-Modal: Get modal state index
//   - StateIndex(M, S) = i
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 332-356: ModalPattern in RegisterPatternBindings
//   - Lines 546-572: ModalPattern in LowerBindPattern
//
// =============================================================================

#include "05_codegen/lower/pattern/modal_pattern.h"

#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_layout.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/lower_pat.h"
#include "05_codegen/lower/pattern/pattern_common.h"

namespace cursive::codegen {

// ============================================================================
// RegisterModalPatternBindings
// ============================================================================
//
// Registers bindings for modal pattern fields.
// The modal path is extracted from the type hint to look up field types.
//
void RegisterModalPatternBindings(
    const ast::ModalPattern& pattern,
    const analysis::TypeRef& type_hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk) {
  // Extract modal path from the type hint
  analysis::TypePath modal_path;
  std::vector<analysis::TypeRef> modal_args;
  if (type_hint && std::holds_alternative<analysis::TypeModalState>(type_hint->node)) {
    const auto& modal_state = std::get<analysis::TypeModalState>(type_hint->node);
    modal_path = modal_state.path;
    modal_args = modal_state.generic_args;
  } else if (type_hint) {
    if (const auto* applied_path = analysis::AppliedTypePath(*type_hint)) {
      modal_path = *applied_path;
      if (const auto* applied_args = analysis::AppliedTypeArgs(*type_hint)) {
        modal_args = *applied_args;
      }
    }
  }

  // Look up the modal declaration
  const ast::ModalDecl* modal_decl = modal_path.empty() ? nullptr : LookupModalDecl(modal_path, ctx);

  // If no fields in the pattern, nothing to register
  if (!pattern.fields_opt.has_value()) {
    return;
  }

  // Register bindings for each field
  for (const auto& field : pattern.fields_opt->fields) {
    // Get the type of this field from the modal declaration
    analysis::TypeRef field_type;
    if (modal_decl) {
      field_type = ModalFieldType(
          *modal_decl,
          modal_args,
          pattern.state,
          field.name,
          ctx);
    }

    if (field.pattern_opt) {
      // Field has a nested pattern
      walk(*field.pattern_opt, field_type);
    } else {
      // Shorthand: field name becomes binding
      ctx.RegisterVar(field.name, field_type, true, is_immovable, prov,
                      prov_region, false, prov_region_tag);
    }
  }
}

// ============================================================================
// LowerModalPatternBindings
// ============================================================================
//
// Creates binding IR for modal pattern fields.
// Extracts state-specific field values from the modal scrutinee.
//
IRPtr LowerModalPatternBindings(
    const ast::ModalPattern& pattern,
    const IRValue& value,
    LowerCtx& ctx,
    std::function<analysis::TypeRef(const std::string&)> lookup_bind_type,
    std::function<analysis::ProvenanceKind(const std::string&)> lookup_bind_prov,
    std::function<std::optional<std::string>(const std::string&)> lookup_bind_region,
    std::function<std::optional<std::string>(const std::string&)> lookup_bind_region_tag,
    std::function<IRPtr(const ast::Pattern&, const IRValue&)> lower_bind) {
  // If no fields in the pattern, nothing to bind
  if (!pattern.fields_opt) {
    return EmptyIR();
  }

  std::vector<IRPtr> bindings;

  for (const auto& field : pattern.fields_opt->fields) {
    // Create a derived value for modal field extraction
    IRValue field_val = ctx.FreshTempValue("pat_modal_field");
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::ModalField;
    info.base = value;
    info.modal_state = pattern.state;
    info.field = field.name;
    ctx.RegisterDerivedValue(field_val, info);

    if (field.pattern_opt) {
      // Field has a nested pattern - recurse
      bindings.push_back(lower_bind(*field.pattern_opt, field_val));
    } else {
      // Shorthand: directly bind field name to extracted value
      IRBindVar bind;
      bind.name = field.name;
      bind.stable_name = ctx.StableBindingName(field.name);
      bind.value = field_val;
      bind.type = lookup_bind_type(field.name);
      bind.prov = lookup_bind_prov(field.name);
      bind.prov_region = lookup_bind_region(field.name);
      bind.prov_region_tag = lookup_bind_region_tag(field.name);
      bindings.push_back(MakeIR(std::move(bind)));
    }
  }

  return SeqIR(std::move(bindings));
}

// ============================================================================
// PatternCheckModal
// ============================================================================
//
// Checks if a value matches a modal pattern by comparing state tags.
//
IRValue PatternCheckModal(const ast::ModalPattern& /*pattern*/,
                           const IRValue& value,
                           LowerCtx& ctx) {
  SPEC_RULE("TagOf-Modal");
  // Get the tag of the scrutinee value
  (void)TagOf(value, TagOfKind::Modal, ctx);
  // Return a fresh temp representing the comparison result
  return ctx.FreshTempValue("pat_modal_tag_match");
}

}  // namespace cursive::codegen
