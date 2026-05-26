#pragma once

#include <optional>
#include <string>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "04_analysis/memory/regions.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// ============================================================================
// Binding Registration
// ============================================================================

// Register bindings from a pattern into the lowering context.
// This walks the pattern recursively and calls ctx.RegisterVar for each
// identifier binding found.
void RegisterBindingsFromPattern(const ast::Pattern& pattern,
                                 const analysis::TypeRef& type_hint,
                                 LowerCtx& ctx,
                                 bool is_immovable = false,
                                 analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom,
                                 std::optional<std::string> prov_region = std::nullopt,
                                 std::optional<std::string> prov_region_tag = std::nullopt,
                                 bool has_responsibility = true);

// ============================================================================
// Binding Emission
// ============================================================================

// Emit an IRBindVar node for a single binding.
IRPtr EmitBinding(const std::string& name,
                  const IRValue& value,
                  const analysis::TypeRef& type,
                  analysis::ProvenanceKind prov,
                  std::optional<std::string> prov_region,
                  std::optional<std::string> prov_region_tag,
                  LowerCtx& ctx);

// ============================================================================
// Provenance Helpers
// ============================================================================

// Get the provenance kind for a binding's initializer expression.
analysis::ProvenanceKind GetBindingProvenance(const ast::Binding& binding,
                                              LowerCtx& ctx);

// Get the region name (if any) for a binding's initializer expression.
std::optional<std::string> GetBindingRegion(const ast::Binding& binding,
                                            LowerCtx& ctx);
std::optional<std::string> GetBindingRegionTag(const ast::Binding& binding,
                                               LowerCtx& ctx);

}  // namespace ultraviolet::codegen
