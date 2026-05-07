// =============================================================================
// MIGRATION MAPPING: loop_conditional.cpp
// =============================================================================
// This file implements parsing for conditional loop expressions
// (loop condition { body } - equivalent to while loops).
//
// =============================================================================
// SPEC REFERENCE: CursiveSpecification.md
// =============================================================================
//
// **(Parse-Loop-Expr)** Lines 5259-5262:
// IsKw(Tok(P), `loop`)    Γ ⊢ ParseLoopTail(Advance(P)) ⇓ (P_1, loop)
// ────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParsePrimary(P) ⇓ (P_1, loop)
//
// **(Parse-LoopTail-Cond)** Lines 6001-6004:
// Γ ⊢ ParseExpr_NoBrace(P) ⇓ (P_1, cond)
// Γ ⊢ ParseLoopInvariantOpt(P_1) ⇓ (P_2, inv_opt)
// Γ ⊢ ParseBlock(P_2) ⇓ (P_3, body)
// ────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseLoopTail(P) ⇓ (P_3, LoopConditional(cond, inv_opt, body))
//
// SEMANTICS:
// - Conditional loop: `loop condition { body }` (like while loops)
// - Condition is parsed with ParseExpr_NoBrace (no brace in condition)
// - Optional invariant clause: `where { predicate }`
// - Body is a block expression (braces required)
// - Loop executes while condition is true
// - Can exit early via `break` or `return`
// - Can produce a value via `break value`
//
// DISAMBIGUATION:
// - If token after "loop" is "{" or "where" -> infinite loop
// - If TryParsePatternIn succeeds (pattern followed by "in") -> iterator loop
// - Otherwise -> conditional loop (this file)
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
// =============================================================================
//
// 1. LOOP EXPRESSION PARSING (within ParsePrimary)
//    Source lines: 1484-1490
//    -------------------------------------------------------------------------
//    if (tok && IsKwTok(*tok, "loop")) {
//      SPEC_RULE("Parse-Loop-Expr");
//      Parser next = parser;
//      Advance(next);
//      LoopTailResult tail = ParseLoopTail(next);
//      return {tail.parser, tail.loop_expr};
//    }
//
// 2. ParseLoopTail - CONDITIONAL LOOP CASE (fallback)
//    Source lines: 2510-2518
//    -------------------------------------------------------------------------
//    // After checking for infinite and iterator cases:
//    SPEC_RULE("Parse-LoopTail-Cond");
//    ParseElemResult<ExprPtr> cond = ParseExprNoBrace(parser);
//    ParseElemResult<std::optional<LoopInvariant>> inv = ParseLoopInvariantOpt(cond.parser);
//    ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(inv.parser);
//    LoopConditionalExpr loop;
//    loop.cond = cond.elem;
//    loop.invariant_opt = inv.elem;
//    loop.body = body.elem;
//    return {body.parser, MakeExpr(SpanBetween(parser, body.parser), loop)};
//
// 3. FULL ParseLoopTail IMPLEMENTATION (for context)
//    Source lines: 2473-2519
//    -------------------------------------------------------------------------
//    LoopTailResult ParseLoopTail(Parser parser) {
//      // Case 1: Infinite loop (starts with { or where)
//      if (IsPunc(parser, "{") || IsKw(parser, "where")) {
//        SPEC_RULE("Parse-LoopTail-Infinite");
//        // ... infinite loop handling ...
//      }
//
//      // Case 2: Iterator loop (pattern ... in iterable)
//      TryPatternInResult try_in = TryParsePatternIn(parser);
//      if (try_in.ok) {
//        SPEC_RULE("Parse-LoopTail-Iter");
//        // ... iterator loop handling ...
//      }
//
//      // Case 3: Conditional loop (fallback)
//      SPEC_RULE("Parse-LoopTail-Cond");
//      ParseElemResult<ExprPtr> cond = ParseExprNoBrace(parser);
//      ParseElemResult<std::optional<LoopInvariant>> inv = ParseLoopInvariantOpt(cond.parser);
//      ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(inv.parser);
//      LoopConditionalExpr loop;
//      loop.cond = cond.elem;
//      loop.invariant_opt = inv.elem;
//      loop.body = body.elem;
//      return {body.parser, MakeExpr(SpanBetween(parser, body.parser), loop)};
//    }
//
// =============================================================================
// DEPENDENCIES
// =============================================================================
// FROM expr_common.cpp:
// - MakeExpr(Span, ExprNode) -> ExprPtr
// - SpanBetween(Parser, Parser) -> Span
//
// FROM parser_common.cpp:
// - Parser state type
// - Advance(Parser&) -> void
// - Tok(Parser) -> const Token*
// - IsKw(Parser, string_view) -> bool
// - IsPunc(Parser, string_view) -> bool
//
// FROM loop_infinite.cpp (or loop_common.cpp):
// - ParseLoopInvariantOpt(Parser) -> ParseElemResult<std::optional<LoopInvariant>>
//
// FROM block_expr.cpp (or parser_stmt.cpp):
// - ParseBlock(Parser) -> ParseElemResult<std::shared_ptr<Block>>
//
// FROM binary.cpp (or expr entry):
// - ParseExprNoBrace(Parser) -> ParseElemResult<ExprPtr>
//
// FROM loop_iter.cpp (for disambiguation):
// - TryParsePatternIn(Parser) -> TryPatternInResult
//
// FROM AST types:
// - LoopConditionalExpr struct {
//     ExprPtr cond;
//     std::optional<LoopInvariant> invariant_opt;
//     std::shared_ptr<Block> body;
//   }
// - LoopInvariant struct { ExprPtr predicate; Span span; }
// - Block struct { std::vector<Stmt> stmts; ExprPtr tail_opt; Span span; }
// - ExprPtr = std::shared_ptr<Expr>
//
// =============================================================================
// REFACTORING NOTES
// =============================================================================
// 1. Conditional loop is the FALLBACK case after checking infinite and iterator.
//    The order of checks matters for correct parsing.
//
// 2. ParseLoopTail dispatches to three different loop types. Consider:
//    - Keeping one ParseLoopTail in a loop_dispatch.cpp
//    - OR having each loop type check its precondition and return nullopt
//
// 3. The condition uses ParseExprNoBrace to prevent ambiguity with body block.
//    Without this restriction, `loop x { ... }` would be ambiguous.
//
// 4. Export standalone functions:
//    ParseElemResult<ExprPtr> ParseLoopConditionalExpr(Parser parser);
//    - Assumes parser is positioned AFTER the "loop" keyword
//    - Caller responsible for ensuring it's not infinite or iterator loop
//
// 5. Alternative: Export a TryParseLoopConditional that can fail if the
//    pattern-in lookahead succeeds (delegating to iterator).
//
// 6. Loop invariant is optional and parsed BETWEEN condition and body:
//    `loop cond where { inv } { body }`
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsPunc(const Parser& parser, std::string_view punc);
ParseElemResult<ExprPtr> ParseExprNoBrace(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

// Forward declaration from loop_infinite.cpp
ParseElemResult<std::optional<LoopInvariant>> ParseLoopInvariantOpt(Parser parser);

// =============================================================================
// ParseLoopConditionalExpr - Parse conditional loop expression
// =============================================================================
//
// SPEC: Lines 6001-6004
// loop condition { body } or loop condition where { invariant } { body }
// Assumes "loop" keyword already consumed.
// Caller responsible for ensuring this is not an infinite or iterator loop.

ParseElemResult<ExprPtr> ParseLoopConditionalExpr(Parser parser) {
  SPEC_RULE("Parse-LoopTail-Cond");
  Parser start = parser;

  // Parse condition expression (no braces allowed to avoid ambiguity with body)
  ParseElemResult<ExprPtr> cond = ParseExprNoBrace(parser);

  // Parse optional invariant clause
  ParseElemResult<std::optional<LoopInvariant>> inv = ParseLoopInvariantOpt(cond.parser);

  // Parse body block
  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(inv.parser);

  // Build LoopConditionalExpr
  LoopConditionalExpr loop;
  loop.cond = cond.elem;
  loop.invariant_opt = inv.elem;
  loop.body = body.elem;

  return {body.parser, MakeExpr(SpanBetween(start, body.parser), loop)};
}

}  // namespace cursive::ast
