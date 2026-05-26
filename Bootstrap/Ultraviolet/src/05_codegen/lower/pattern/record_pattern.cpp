// =============================================================================
// MIGRATION MAPPING: pattern/record_pattern.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Record patterns destructure record values
//   - Field patterns bind to record field values
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_pat.cpp
//   - Lines 280-294: RecordPattern in RegisterPatternBindings
//   - Looks up RecordDecl to get field types
//   - Shorthand x vs explicit x: pattern handled
//   - LowerBindPattern: field access for each binding
//
// DEPENDENCIES:
//   - ultraviolet/src/05_codegen/ir_model.h (IRFieldAccess)
//   - Helper functions: LookupRecordDecl, RecordFieldType
//
// =============================================================================

#include "05_codegen/lower/pattern/record_pattern.h"

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/layout/layout.h"
#include "02_source/ast/ast.h"

#include <cassert>
#include <variant>

namespace ultraviolet::codegen {

namespace {

// ============================================================================
// Helper functions for record pattern lowering
// ============================================================================

/// Lower a syntax type to analysis TypeRef for layout computation
static analysis::TypeRef LowerSyntaxType(const std::shared_ptr<ast::Type>& type,
                                         LowerCtx& ctx) {
  if (!type) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::ultraviolet::analysis::layout::LowerTypeForLayout(scope, type)) {
    return *lowered;
  }
  return nullptr;
}

/// Look up a RecordDecl from a type path in the Sigma environment
static const ast::RecordDecl* LookupRecordDecl(const ast::TypePath& path,
                                               const LowerCtx& ctx) {
  if (!ctx.sigma) {
    return nullptr;
  }
  const auto it = ctx.sigma->types.find(analysis::PathKeyOf(path));
  if (it == ctx.sigma->types.end()) {
    return nullptr;
  }
  return std::get_if<ast::RecordDecl>(&it->second);
}

/// Get the type of a field in a record declaration
static analysis::TypeRef RecordFieldType(const ast::RecordDecl& decl,
                                         std::string_view name,
                                         LowerCtx& ctx) {
  for (const auto& member : decl.members) {
    if (const auto* field = std::get_if<ast::FieldDecl>(&member)) {
      if (analysis::IdEq(field->name, name)) {
        return LowerSyntaxType(field->type, ctx);
      }
    }
  }
  return nullptr;
}

}  // namespace

// ============================================================================
// RegisterRecordPatternBindings - Register bindings for a record pattern
// ============================================================================
//
// Called from RegisterPatternBindings in pattern_common.cpp for RecordPattern.
// Looks up the record declaration and recursively registers bindings for
// each field. Supports both shorthand (Point{x}) and explicit (Point{x: a})
// field patterns.
//
// SPEC: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Record patterns destructure record values to their fields
//   - Each field pattern can be a shorthand binding or nested pattern
// ============================================================================

void RegisterRecordPatternBindings(
    const ast::RecordPattern& node,
    const analysis::TypeRef& hint,
    LowerCtx& ctx,
    bool is_immovable,
    analysis::ProvenanceKind prov,
    std::optional<std::string> prov_region,
    std::optional<std::string> prov_region_tag,
    std::function<void(const ast::Pattern&, analysis::TypeRef)> walk) {
  // Look up the record declaration from the path
  const ast::RecordDecl* record = LookupRecordDecl(node.path, ctx);

  // Register bindings for each field pattern
  for (const auto& field : node.fields) {
    // Get the type of this field from the record declaration
    analysis::TypeRef field_type;
    if (record) {
      field_type = RecordFieldType(*record, field.name, ctx);
    }

    if (field.pattern_opt) {
      // Explicit pattern: Point{x: some_pattern}
      // Recursively walk the nested pattern with the field type as hint
      walk(*field.pattern_opt, field_type);
    } else {
      // Shorthand pattern: Point{x}
      // Register binding for the field name directly
      ctx.RegisterVar(field.name, field_type, true, is_immovable, prov,
                      prov_region, false, prov_region_tag);
    }
  }
}

// ============================================================================
// LowerBindRecordPattern - Lower binding for a record pattern
// ============================================================================
//
// Called from LowerBindPattern in pattern_common.cpp for RecordPattern.
// Creates derived values for field extraction and recursively binds
// each field to its corresponding pattern.
//
// SPEC: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Extracts field values from the record scrutinee
//   - Lower-BindList-Cons: SeqIR(BindVarIR(x, v), IR_r)
// ============================================================================

IRPtr LowerBindRecordPattern(
    const ast::RecordPattern& pat,
    const IRValue& value,
    LowerCtx& ctx,
    std::function<analysis::TypeRef(const std::string&)> lookup_bind_type,
    std::function<analysis::ProvenanceKind(const std::string&)> lookup_bind_prov,
    std::function<std::optional<std::string>(const std::string&)>
        lookup_bind_region,
    std::function<std::optional<std::string>(const std::string&)>
        lookup_bind_region_tag,
    std::function<IRPtr(const ast::Pattern&, const IRValue&)> lower_bind) {
  std::vector<IRPtr> bindings;

  // Process each field in the record pattern
  for (const auto& field : pat.fields) {
    // Create a derived value representing field access
    IRValue field_val = ctx.FreshTempValue("pat_field");
    DerivedValueInfo info;
    info.kind = DerivedValueInfo::Kind::Field;
    info.base = value;
    info.field = field.name;
    ctx.RegisterDerivedValue(field_val, info);

    if (field.pattern_opt) {
      // Explicit pattern: Point{x: some_pattern}
      // Recursively lower the binding for the nested pattern
      bindings.push_back(lower_bind(*field.pattern_opt, field_val));
    } else {
      // Shorthand pattern: Point{x}
      // Directly bind the field name to the extracted field value
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
// PatternCheckRecord - Check if a value matches a record pattern
// ============================================================================
//
// Called from PatternCheck in pattern_common.cpp for RecordPattern.
// Record patterns are always irrefutable (they always match if types are
// correct), so this simply returns a constant true value.
//
// SPEC: Docs/SPECIFICATION.md Section 6.6 (Pattern Matching Lowering)
//   - Record patterns are irrefutable (no discriminant to check)
// ============================================================================

IRValue PatternCheckRecord(const ast::RecordPattern& pat,
                           const IRValue& value,
                           LowerCtx& ctx) {
  // Record patterns are irrefutable - they always match
  // Return a constant true value
  IRValue result;
  result.kind = IRValue::Kind::Immediate;
  result.name = "true";
  result.bytes = {static_cast<std::uint8_t>(1)};
  return result;
}

}  // namespace ultraviolet::codegen
