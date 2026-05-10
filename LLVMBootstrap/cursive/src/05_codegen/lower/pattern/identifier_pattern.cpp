// =============================================================================
// Identifier Pattern Lowering Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.6 (Pattern Matching Lowering)
//   - Identifier pattern binds the matched value to a name
//   - Lower-BindList-Cons: SeqIR(BindVarIR(x, v), IR_r)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 255-258: IdentifierPattern in RegisterPatternBindings
//   - Lines 405-412: IdentifierPattern in LowerBindPattern
//
// =============================================================================

#include "05_codegen/lower/pattern/identifier_pattern.h"

#include "00_core/assert_spec.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

// ============================================================================
// RegisterIdentifierPatternBindings
// ============================================================================
//
// Registers a single binding for the identifier pattern.
// Called from the top-level RegisterPatternBindings dispatcher.
//
void RegisterIdentifierPatternBindings(const ast::IdentifierPattern& pattern,
                                        const analysis::TypeRef& type_hint,
                                        LowerCtx& ctx,
                                        bool is_immovable,
                                        analysis::ProvenanceKind prov,
                                        std::optional<std::string> prov_region,
                                        std::optional<std::string> prov_region_tag) {
  ctx.RegisterVar(pattern.name, type_hint, true, is_immovable, prov,
                  prov_region, false, prov_region_tag);
}

// ============================================================================
// LowerIdentifierPatternBindings
// ============================================================================
//
// Creates the binding IR for an identifier pattern.
// Simply creates an IRBindVar node that binds the name to the value.
//
IRPtr LowerIdentifierPatternBindings(const ast::IdentifierPattern& pattern,
                                      const IRValue& value,
                                      LowerCtx& ctx) {
  // Look up binding info from context
  auto lookup_bind_type = [&ctx](const std::string& name) -> analysis::TypeRef {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->type;
    }
    return nullptr;
  };
  auto lookup_bind_prov = [&ctx](const std::string& name) -> analysis::ProvenanceKind {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov;
    }
    return analysis::ProvenanceKind::Bottom;
  };
  auto lookup_bind_region = [&ctx](const std::string& name) -> std::optional<std::string> {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov_region;
    }
    return std::nullopt;
  };
  auto lookup_bind_region_tag = [&ctx](const std::string& name) -> std::optional<std::string> {
    if (const auto* state = ctx.GetBindingState(name)) {
      return state->prov_region_tag;
    }
    return std::nullopt;
  };

  IRBindVar bind;
  bind.name = pattern.name;
  bind.stable_name = ctx.StableBindingName(pattern.name);
  bind.value = value;
  bind.type = lookup_bind_type(pattern.name);
  bind.prov = lookup_bind_prov(pattern.name);
  bind.prov_region = lookup_bind_region(pattern.name);
  bind.prov_region_tag = lookup_bind_region_tag(pattern.name);

  // Preserve structured-value provenance for local bindings (e.g. closures as
  // tuple-literals) so later lowering/emission can recover callee metadata.
  if (const DerivedValueInfo* derived = ctx.LookupDerivedValue(value)) {
    IRValue local_value;
    local_value.kind = IRValue::Kind::Local;
    local_value.name = pattern.name;
    ctx.RegisterDerivedValue(local_value, *derived);
  }

  return MakeIR(std::move(bind));
}

// ============================================================================
// PatternCheckIdentifier
// ============================================================================
//
// Identifier patterns are irrefutable (always match).
// Returns a constant true value.
//
IRValue PatternCheckIdentifier(const ast::IdentifierPattern& /*pattern*/,
                                const IRValue& /*value*/,
                                LowerCtx& /*ctx*/) {
  // Identifier patterns always match
  IRValue result;
  result.kind = IRValue::Kind::Immediate;
  result.name = "true";
  result.bytes = {static_cast<std::uint8_t>(1)};
  return result;
}

}  // namespace cursive::codegen
