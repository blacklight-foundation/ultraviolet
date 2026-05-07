// =============================================================================
// block_expr.cpp - Block Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.3.6 (Lines 5369-5372) - Parse-Block-Expr
//
// This file implements:
//   - ParseBlockExpr: Parse a block expression { stmt* expr? }
//
// The actual block parsing is delegated to ParseBlock in stmt_common.cpp.
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace cursive::ast {

// Forward declarations
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// =============================================================================
// ParseBlockExpr - Parse block expression
// =============================================================================
//
// SPEC: Lines 5369-5372
// Block as an expression node. Wraps the Block in a BlockExpr.
// Assumes "{" already checked by caller.

ParseElemResult<ExprPtr> ParseBlockExpr(Parser parser) {
  SPEC_RULE("Parse-Block-Expr");
  Parser start = parser;
  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(parser);
  BlockExpr blk;
  blk.block = block.elem;
  return {block.parser, MakeExpr(SpanBetween(start, block.parser), blk)};
}

}  // namespace cursive::ast
