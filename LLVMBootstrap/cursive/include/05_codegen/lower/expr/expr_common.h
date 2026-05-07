#pragma once

// =============================================================================
// Expression Lowering Common Utilities
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - LowerExpr judgment (Lines 16048+)
//   - Place representation (Lines 16098+)
//   - Evaluation order (Lines 16024+)
//
// This header provides internal utilities shared across expression lowering
// implementations. The main public API is in lower_expr.h.
//
// CONTENTS:
//   - Literal parsing utilities
//   - Place helper functions (place root, field head)
//   - Expression classification utilities
//   - Attribute handling helpers
//
// =============================================================================

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "05_codegen/lower/lower_expr.h"

namespace cursive::codegen {

// =============================================================================
// §6.4 Literal Parsing Utilities
// =============================================================================

// Strip integer suffix (i8, u16, i32, etc.) from a lexeme
std::string StripIntSuffix(const std::string& text);

// Parse an integer literal lexeme to a numeric value
// Handles binary (0b), octal (0o), decimal, and hex (0x) formats
std::optional<std::uint64_t> ParseIntLiteralLexeme(const std::string& lexeme);

// Encode a uint64 value as big-endian bytes
std::vector<std::uint8_t> EncodeU64BE(std::uint64_t value);

// =============================================================================
// §6.4 Expression Classification
// =============================================================================

// Check if an expression is a place expression (suitable for address-of, move)
// Place expressions: identifiers, field access, tuple access, index, deref
bool IsPlaceExpr(const ast::Expr& expr);

// Check if an expression is a temporary value expression (not a place)
bool IsTempValueExpr(const ast::Expr& expr);

// Check if an expression requires index bounds checking
// Returns false for static arrays if dynamic_checks is false
bool NeedsIndexCheck(const ast::Expr& base, const LowerCtx& ctx);

// Check if an index expression should lower through the range-index path.
// This accepts both literal range syntax and any expression typed as a range.
bool IsRangeIndexExpr(const ast::Expr& expr, const LowerCtx& ctx);

// Recover the semantic kind of a range-valued index expression.
std::optional<ast::RangeKind> RangeIndexKindOf(const ast::Expr& expr,
                                               const LowerCtx& ctx);

// Check if an expression is a move expression
bool IsMoveExpr(const ast::ExprPtr& expr);

// =============================================================================
// §6.4 Place Analysis
// =============================================================================

// Get the root binding name of a place expression
// For x.y.z returns "x", for arr[i].f returns "arr"
std::optional<std::string> PlaceRoot(const ast::Expr& expr);

// Get the first field name in a place path (if any)
// For x.y.z returns "y", for x returns nullopt
std::optional<std::string> FieldHead(const ast::Expr& expr);

// Build a string representation of a place expression
// For x.y.z returns "x.y.z", for arr[0] returns "arr[0]"
std::string BuildPlaceRepr(const ast::Expr& expr);

// =============================================================================
// §6.4 Attribute Handling
// =============================================================================

// Check if attributes contain [[dynamic]]
bool HasDynamicAttr(const ast::AttributeList& attrs);

// Check if attributes contain a memory-order annotation and recover its value.
bool HasMemoryOrderAttr(const ast::AttributeList& attrs);
std::optional<AccessOrdering> MemoryOrderFromAttrs(const ast::AttributeList& attrs);

// Shared/keyed access detection for ordered-access lowering.
bool IsSharedAccessExpr(const ast::Expr& expr, const LowerCtx& ctx);
LowerResult ApplyEffectiveOrdering(const ast::Expr& expr,
                                   LowerResult result,
                                   LowerCtx& ctx);
std::string EncodeLoweredKeyPath(const analysis::KeyPath& path);
IRPtr LowerImplicitKeyAccess(const ast::Expr& expr,
                             ast::KeyMode mode,
                             LowerCtx& ctx);

// Establish the procedure-local region used by synthesized callable bodies
// such as spawn and dispatch wrappers. The caller must have pushed the wrapper
// root scope before calling this and must pop ctx.active_region_aliases after
// lowering the body, before computing cleanup for that scope.
IRPtr EnterSyntheticProcedureRegion(LowerCtx& ctx);

// =============================================================================
// §6.4 Range Formatting (for error messages/debugging)
// =============================================================================

// Format a range expression as a string (e.g., "0..10")
std::string FormatRangeExpr(const ast::RangeExpr& expr);

// Format an index expression as a string
std::string FormatIndexExpr(const ast::Expr& expr);

// =============================================================================
// §6.4 Argument Expression Extraction
// =============================================================================

// Extract expression pointers from argument list
std::vector<ast::ExprPtr> ArgsExprs(const std::vector<ast::Arg>& args);

// Extract expression pointers from field initializer list
std::vector<ast::ExprPtr> FieldExprs(const std::vector<ast::FieldInit>& fields);

// =============================================================================
// §6.4 Parallel/Dispatch Expression Detection
// =============================================================================

// Check if a dispatch expression has a reduce option
bool DispatchHasReduce(const ast::DispatchExpr& expr);

// Check if an expression is collectable in a parallel block
// (spawn or dispatch with reduce)
bool IsCollectableParallelExpr(const ast::Expr& expr, bool& needs_wait);

// =============================================================================
// §6.4 Block Expression Utilities
// =============================================================================

// Wrap a Block in a BlockExpr expression
ast::ExprPtr WrapBlockExpr(const std::shared_ptr<ast::Block>& block);

// =============================================================================
// §6.4 Update binding state after partial assignment
// =============================================================================

// Update binding state after a field is assigned (clears moved field status)
void UpdateBindingAfterFieldAssign(const ast::Expr& place, LowerCtx& ctx);

// =============================================================================
// §6.4 Place Lowering
// =============================================================================

// Lower a place expression to its representation
IRPlace LowerPlace(const ast::Expr& place, LowerCtx& ctx);

// =============================================================================
// §6.4 Module Path Utilities
// =============================================================================

// Split a module path string "a::b::c" into ["a", "b", "c"]
std::vector<std::string> SplitModulePathString(const std::string& module);

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorExprCommonRules();

}  // namespace cursive::codegen
