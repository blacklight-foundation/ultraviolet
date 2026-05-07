#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// ============================================================================
// §6.6 Pattern Lowering Judgments
// ============================================================================

// §6.6 LowerBindPattern - bind a value to a pattern
IRPtr LowerBindPattern(const ast::Pattern& pattern,
                       const IRValue& value,
                       LowerCtx& ctx);

// §6.6 LowerBindList - bind values to a list of patterns
IRPtr LowerBindList(const std::vector<std::shared_ptr<ast::Pattern>>& patterns,
                    const std::vector<IRValue>& values,
                    LowerCtx& ctx);

// Register bindings introduced by a pattern with optional type hints
void RegisterPatternBindings(const ast::Pattern& pattern,
                             const analysis::TypeRef& type_hint,
                             LowerCtx& ctx,
                             bool is_immovable = false,
                             analysis::ProvenanceKind prov = analysis::ProvenanceKind::Bottom,
                             std::optional<std::string> prov_region = std::nullopt,
                             std::optional<std::string> prov_region_tag = std::nullopt,
                             bool has_responsibility = true);

// §6.6 TagOf - get the discriminant tag
enum class TagOfKind { Enum, Modal };
IRValue TagOf(const IRValue& value, TagOfKind kind, LowerCtx& ctx);

// ============================================================================
// §6.6 If-Is Case Analysis Lowering
// ============================================================================

// §6.6 LowerIfCases - lower if-is case analysis
LowerResult LowerIfCases(const ast::Expr& scrutinee,
                         const std::vector<ast::IfCaseClause>& arms,
                         const ast::ExprPtr& else_expr,
                         bool single_form,
                         LowerCtx& ctx);

// §6.6 LowerIfCaseClause - lower a single case clause
LowerResult LowerIfCaseClause(const ast::IfCaseClause& arm,
                          const IRValue& scrutinee,
                          const analysis::TypeRef& scrutinee_type,
                          analysis::ProvenanceKind scrutinee_prov,
                          std::optional<std::string> scrutinee_region,
                          std::optional<std::string> scrutinee_region_tag,
                          LowerCtx& ctx);

// §6.6 PatternCheck - check if a value matches a pattern
IRValue PatternCheck(const ast::Pattern& pattern,
                     const IRValue& value,
                     LowerCtx& ctx);

// Emit SPEC_RULE anchors for all §6.6 rules
void AnchorPatternRules();

}  // namespace cursive::codegen
