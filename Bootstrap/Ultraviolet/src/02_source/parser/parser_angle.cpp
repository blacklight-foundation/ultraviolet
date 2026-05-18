// =============================================================================
// parser_angle.cpp - Angle Bracket Handling for Generics
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.3 (Lines 3004-3016)
//
// This file implements angle bracket handling for generic parameters/arguments:
//   - SplitSpan2: Split a 2-character span into two 1-character spans
//   - SplitShiftR: Split ">>" token into two ">" tokens
//   - AngleDelta: Compute depth change from angle bracket tokens
//   - AngleStep: Advance parser and update angle bracket depth
//   - AngleScan: Scan forward to find matching closing angle bracket
//   - SkipAngles: Convenience wrapper to skip angle-bracketed content
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <utility>
#include <vector>

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// =============================================================================
// SplitSpan2 - Split a 2-character span into two 1-character spans
// =============================================================================
//
// SPEC: Section 3.3.3 lines 3004-3011
//   SplitSpan2(sp) = (sp_L, sp_R) where:
//   - sp_L.file = sp.file, sp_R.file = sp.file
//   - sp_L.start_offset = sp.start_offset, sp_L.end_offset = sp.start_offset + 1
//   - sp_R.start_offset = sp.start_offset + 1, sp_R.end_offset = sp.start_offset + 2
//   - Similar for line/col fields

std::pair<core::Span, core::Span> SplitSpan2(const core::Span& sp) {
  core::Span left = sp;
  core::Span right = sp;

  left.start_offset = sp.start_offset;
  left.end_offset = sp.start_offset + 1;
  right.start_offset = sp.start_offset + 1;
  right.end_offset = sp.start_offset + 2;

  left.start_line = sp.start_line;
  left.end_line = sp.start_line;
  right.start_line = sp.start_line;
  right.end_line = sp.start_line;

  left.start_col = sp.start_col;
  left.end_col = sp.start_col + 1;
  right.start_col = sp.start_col + 1;
  right.end_col = sp.start_col + 2;

  return {left, right};
}

// =============================================================================
// SplitShiftR - Split ">>" token into two ">" tokens
// =============================================================================
//
// SPEC: Section 3.3.3 lines 3013-3015
//   SplitShiftR(P) = <K', i, D, j, d, Delta>
//   where Tok(P) = <Operator(">>"), ">>", sp> && (sp_L, sp_R) = SplitSpan2(sp)
//   K' = K[0..i) ++ [<Operator(">"), ">", sp_L>, <Operator(">"), ">", sp_R>] ++ K[i+1..]
//
// This is used to handle cases like `Vec<Vec<i32>>` where `>>` must be split
// into two `>` tokens for proper generic argument parsing.

Parser SplitShiftR(const Parser& parser) {
  if (!parser.tokens || parser.index >= parser.tokens->size()) {
    return parser;
  }
  const Token& tok = (*parser.tokens)[parser.index];
  if (tok.kind != TokenKind::Operator || tok.lexeme != ">>") {
    return parser;
  }

  const auto spans = SplitSpan2(tok.span);
  Token left = tok;
  left.kind = TokenKind::Operator;
  left.lexeme = ">";
  left.span = spans.first;

  Token right = tok;
  right.kind = TokenKind::Operator;
  right.lexeme = ">";
  right.span = spans.second;

  std::vector<Token> updated;
  updated.reserve(parser.tokens->size() + 1);
  for (std::size_t i = 0; i < parser.tokens->size(); ++i) {
    if (i == parser.index) {
      updated.push_back(left);
      updated.push_back(right);
      continue;
    }
    updated.push_back((*parser.tokens)[i]);
  }

  Parser out = parser;
  out.owned_tokens = std::make_shared<std::vector<Token>>(std::move(updated));
  out.tokens = out.owned_tokens.get();
  return out;
}

// =============================================================================
// AngleDelta - Compute depth change from angle bracket tokens
// =============================================================================
//
// Returns:
//   +1 for "<" (opening)
//   -1 for ">" (closing)
//   -2 for ">>" (two closings)
//   0 for other tokens

int AngleDelta(const Token& tok) {
  if (tok.kind != TokenKind::Operator) {
    return 0;
  }
  if (tok.lexeme == "<") {
    return 1;
  }
  if (tok.lexeme == ">") {
    return -1;
  }
  if (tok.lexeme == ">>") {
    return -2;
  }
  return 0;
}

// =============================================================================
// AngleStep - Advance parser and update angle bracket depth
// =============================================================================

AngleStepResult AngleStep(const Parser& parser, int depth) {
  AngleStepResult result;
  result.parser = parser;
  result.depth = depth;
  if (const Token* tok = Tok(parser)) {
    result.depth = depth + AngleDelta(*tok);
  }
  Advance(result.parser);
  return result;
}

// =============================================================================
// AngleScan - Scan forward to find matching closing angle bracket
// =============================================================================
//
// Scans forward from parser position to find balanced angle brackets.
// If it reaches EOF without finding balanced brackets, returns the
// start position (indicating no valid generic argument list).

Parser AngleScan(const Parser& start, const Parser& parser, int depth) {
  Parser current = parser;
  int d = depth;
  for (;;) {
    if (AtEof(current)) {
      return start;
    }
    AngleStepResult step = AngleStep(current, d);
    current = step.parser;
    d = step.depth;
    if (d == 0) {
      return current;
    }
  }
}

// =============================================================================
// SkipAngles - Convenience wrapper to skip angle-bracketed content
// =============================================================================
//
// Used for lookahead/validation only; doesn't construct AST.

Parser SkipAngles(const Parser& parser) {
  return AngleScan(parser, parser, 0);
}

}  // namespace ultraviolet::ast
