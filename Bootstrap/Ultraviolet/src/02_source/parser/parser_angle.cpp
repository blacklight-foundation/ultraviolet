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

#include <algorithm>
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
  const Token* tok = Tok(parser);
  if (!parser.tokens || !tok || tok->kind != TokenKind::Operator ||
      tok->lexeme != ">>") {
    return parser;
  }

  std::size_t split_count_before = 0;
  if (parser.split_shift_right_indices) {
    for (const std::size_t split_index : *parser.split_shift_right_indices) {
      const std::size_t split_virtual_index =
          split_index + split_count_before;
      if (parser.index <= split_virtual_index + 1) {
        break;
      }
      ++split_count_before;
    }
  }
  const std::size_t underlying_index = parser.index - split_count_before;

  Parser out = parser;
  auto split_indices = std::make_shared<std::vector<std::size_t>>();
  if (parser.split_shift_right_indices) {
    *split_indices = *parser.split_shift_right_indices;
  }
  const auto insert_at = std::lower_bound(split_indices->begin(),
                                          split_indices->end(),
                                          underlying_index);
  if (insert_at == split_indices->end() || *insert_at != underlying_index) {
    split_indices->insert(insert_at, underlying_index);
  }
  out.split_shift_right_indices = std::move(split_indices);
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
