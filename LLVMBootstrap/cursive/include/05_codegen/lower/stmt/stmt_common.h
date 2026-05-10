#pragma once

// =============================================================================
// Statement Lowering Common Utilities
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.5 (Statement Lowering)
//   - Lines 16586-16756: Statement lowering judgments
//   - LowerStmt dispatch
//   - LowerStmtList, LowerBlock
//   - Temporary cleanup handling
//
// This header provides internal utilities shared across statement lowering
// implementations. The main public API is in lower_stmt.h.
//
// CONTENTS:
//   - Provenance info extraction
//   - Block analysis helpers
//   - Pattern name collection
//   - Loop-specific utilities
//   - Parallel collect scope tracking
//
// =============================================================================

#include <optional>
#include <string>
#include <vector>

#include "05_codegen/lower/lower_stmt.h"
#include "04_analysis/memory/regions.h"

namespace cursive::codegen {

// =============================================================================
// §6.5 Provenance Information
// =============================================================================

// Provenance info for a binding or expression
struct ProvInfo {
  analysis::ProvenanceKind kind = analysis::ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_tag;
  bool fresh_region = false;
};

// Compute provenance for a new binding from initializer provenance
// Bottom => Stack, otherwise preserves the provenance
ProvInfo BindProvInfo(const ProvInfo& init);

// Extract provenance info from an expression via the lowering context
ProvInfo ExprProvInfo(const ast::Expr& expr, const LowerCtx& ctx);

// =============================================================================
// §6.5 Block Analysis
// =============================================================================

// Check if a block ends with a return statement
bool BlockEndsWithReturn(const ast::Block& block);

// Check if a block ends with a break statement
bool BlockEndsWithBreak(const ast::Block& block);

// Check if a block ends with a continue statement
bool BlockEndsWithContinue(const ast::Block& block);

// Check if a block ends with a diverging control flow statement
bool BlockDiverges(const ast::Block& block);

// =============================================================================
// §6.5 Pattern Utilities
// =============================================================================

// Collect all binding names introduced by a pattern
void CollectPatternNames(const ast::Pattern& pattern,
                         std::vector<std::string>& out);

// Get a flat list of binding names from a pattern
std::vector<std::string> PatternNames(const ast::Pattern& pattern);

// =============================================================================
// §6.5 Loop Utilities
// =============================================================================

// Get the element type for a loop iteration variable
// For arrays/slices, returns the element type
analysis::TypeRef LoopPatternType(const analysis::TypeRef& iter_type);

// Lower a type expression for use in binding type
analysis::TypeRef LowerBindingType(const std::shared_ptr<ast::Type>& type_opt,
                                   LowerCtx& ctx);

// =============================================================================
// §6.5 Parallel Collect Scope
// =============================================================================

// RAII scope guard for parallel collection depth tracking
// Used during parallel block lowering to collect spawn/dispatch results
struct ParallelCollectScope {
  LowerCtx& ctx;
  bool active = false;

  explicit ParallelCollectScope(LowerCtx& ctx_in);
  ~ParallelCollectScope();

  // Non-copyable
  ParallelCollectScope(const ParallelCollectScope&) = delete;
  ParallelCollectScope& operator=(const ParallelCollectScope&) = delete;
};

// =============================================================================
// §6.5 Temporary Cleanup
// =============================================================================

// Compute the drop order for temporaries (reverse of creation order)
std::vector<TempValue> TempDropOrder(const std::vector<TempValue>& temps);

// Emit IR to clean up temporaries at statement boundary
IRPtr TempCleanupIR(const std::vector<TempValue>& temps, LowerCtx& ctx);

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorStmtCommonRules();

}  // namespace cursive::codegen
