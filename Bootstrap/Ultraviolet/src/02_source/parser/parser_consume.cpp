// =============================================================================
// parser_consume.cpp - Token Consumption Helpers
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 5.5 (Lines 2836-2921)
//
// This file implements token consumption operations:
//   - ConsumeState / TryAdvanceConsume: canonical token-consumption state model
//   - TokenMatches: Check if token matches a specification
//   - TokenInEndSet: Check if token is in end set (for list parsing)
//   - RecordListStart/RecordListCons/ListDone: canonical list small-step traces
//   - ConsumeKind: Consume token by kind
//   - ConsumeKeyword: Consume keyword token
//   - ConsumeOperator: Consume operator token
//   - ConsumePunct: Consume punctuator token
//   - TrailingComma: Detect trailing comma
//   - TrailingCommaAllowed: Check if trailing comma is valid
//   - EmitTrailingCommaErr: Emit error for invalid trailing comma
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

namespace {

// =============================================================================
// MatchLexeme - Determine if token kind requires lexeme matching
// =============================================================================

bool MatchLexeme(TokenKind kind) {
  return kind == TokenKind::Keyword || kind == TokenKind::Operator ||
         kind == TokenKind::Punctuator;
}

template <typename MatchSpec>
bool TokenMatchesImpl(const Token& tok, const MatchSpec& match) {
  if (tok.kind != match.kind) {
    return false;
  }
  if (MatchLexeme(match.kind)) {
    return tok.lexeme == match.lexeme;
  }
  return true;
}

bool ConsumeByMatch(Parser& parser, TokenKindMatch expected) {
  ConsumeState state = Consume(parser, expected);
  const ConsumePendingState* pending = std::get_if<ConsumePendingState>(&state);
  if (!pending) {
    return false;
  }

  std::optional<ConsumeDoneState> done = TryAdvanceConsume(*pending);
  if (!done) {
    return false;
  }

  parser = std::move(done->parser);
  return true;
}

struct InvalidTrailingCommaEmitState {
  const Token* comma = nullptr;
  const Token* end_tok = nullptr;
};

std::optional<InvalidTrailingCommaEmitState> InvalidTrailingCommaForEmit(
    const Parser& parser, std::span<const EndSetToken> end_set) {
  const Token* comma = Tok(parser);
  if (!comma || comma->kind != TokenKind::Punctuator ||
      comma->lexeme != ",") {
    return std::nullopt;
  }

  const Parser next = AdvanceOrEOF(parser);
  const Token* end_tok = Tok(next);
  if (!end_tok || !TokenInEndSet(*end_tok, end_set)) {
    return std::nullopt;
  }

  if (comma->span.start_line < end_tok->span.start_line) {
    return std::nullopt;
  }

  return InvalidTrailingCommaEmitState{comma, end_tok};
}

}  // namespace

// =============================================================================
// TryAdvanceConsume - Execute the canonical Consume(P, k) -> ConsumeDone(P)
// =============================================================================
//
// SPEC: ConsumeState / Tok-Consume-* (lines 2836-2857)

std::optional<ConsumeDoneState> TryAdvanceConsume(
    const ConsumePendingState& state) {
  const Token* tok = Tok(state.parser);
  if (!tok || !TokenMatches(*tok, state.expected)) {
    return std::nullopt;
  }

  Parser next = state.parser;
  Advance(next);
  return ConsumeDone(std::move(next));
}

// =============================================================================
// TokenMatches - Check if token matches specification
// =============================================================================
//
// Checks if a token matches a TokenKindMatch specification.
// For keywords, operators, and punctuators, both kind and lexeme must match.
// For other token kinds, only the kind needs to match.

bool TokenMatches(const Token& tok, const TokenKindMatch& match) {
  return TokenMatchesImpl(tok, match);
}

bool TokenMatches(const Token& tok, const EndSetToken& match) {
  return TokenMatchesImpl(tok, match);
}

// =============================================================================
// TokenInEndSet - Check if token is in end set
// =============================================================================
//
// SPEC: Section 5.5 lines 2896-2921 (List Parsing)
// Used for list parsing to detect list terminators.

bool TokenInEndSet(const Token& tok,
                   std::span<const TokenKindMatch> end_set) {
  for (const auto& match : end_set) {
    if (TokenMatches(tok, match)) {
      return true;
    }
  }
  return false;
}

bool TokenInEndSet(const Token& tok, std::span<const EndSetToken> end_set) {
  for (const auto& match : end_set) {
    if (TokenMatches(tok, match)) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// RecordListStart / RecordListCons / ListDone - Canonical List Parsing Steps
// =============================================================================
//
// SPEC: List-Start / List-Cons / List-Done (lines 2898-2909)
// These helpers back the parser-wide list combinators in parser.h so the
// canonical list rules live in a source file that the generated rule registry
// can see, and so real parser paths can emit the generic list traces.

void RecordListStart() { SPEC_RULE("List-Start"); }

void RecordListCons() { SPEC_RULE("List-Cons"); }

bool ListDone(const Parser& parser,
              std::span<const TokenKindMatch> end_set) {
  const Token* tok = Tok(parser);
  if (!tok || !TokenInEndSet(*tok, end_set)) {
    return false;
  }
  SPEC_RULE("List-Done");
  return true;
}

bool ListDone(const Parser& parser, std::span<const EndSetToken> end_set) {
  const Token* tok = Tok(parser);
  if (!tok || !TokenInEndSet(*tok, end_set)) {
    return false;
  }
  SPEC_RULE("List-Done");
  return true;
}

// =============================================================================
// ConsumeKind - Consume token by kind
// =============================================================================
//
// SPEC: Tok-Consume-Kind (lines 2839-2842)
//   Tok(P).kind = k
//   ----------------------------------------
//   <Consume(P, k)> -> <ConsumeDone(Advance(P))>

bool ConsumeKind(Parser& parser, TokenKind kind) {
  if (!ConsumeByMatch(parser, MatchKind(kind))) {
    return false;
  }
  SPEC_RULE("Tok-Consume-Kind");
  return true;
}

// =============================================================================
// ConsumeKeyword - Consume keyword token
// =============================================================================
//
// SPEC: Tok-Consume-Keyword (lines 2844-2847)
//   IsKw(Tok(P), s)
//   ----------------------------------------
//   <Consume(P, Keyword(s))> -> <ConsumeDone(Advance(P))>

bool ConsumeKeyword(Parser& parser, std::string_view keyword) {
  if (!ConsumeByMatch(parser, MatchKeyword(keyword))) {
    return false;
  }
  SPEC_RULE("Tok-Consume-Keyword");
  return true;
}

// =============================================================================
// ConsumeOperator - Consume operator token
// =============================================================================
//
// SPEC: Tok-Consume-Operator (lines 2849-2852)
//   IsOp(Tok(P), s)
//   ----------------------------------------
//   <Consume(P, Operator(s))> -> <ConsumeDone(Advance(P))>

bool ConsumeOperator(Parser& parser, std::string_view op) {
  if (!ConsumeByMatch(parser, MatchOperator(op))) {
    return false;
  }
  SPEC_RULE("Tok-Consume-Operator");
  return true;
}

// =============================================================================
// ConsumePunct - Consume punctuator token
// =============================================================================
//
// SPEC: Tok-Consume-Punct (lines 2854-2857)
//   IsPunc(Tok(P), s)
//   ----------------------------------------
//   <Consume(P, Punctuator(s))> -> <ConsumeDone(Advance(P))>

bool ConsumePunct(Parser& parser, std::string_view punct) {
  if (!ConsumeByMatch(parser, MatchPunct(punct))) {
    return false;
  }
  SPEC_RULE("Tok-Consume-Punct");
  return true;
}

// =============================================================================
// TrailingComma - Detect trailing comma
// =============================================================================
//
// SPEC: TrailingComma predicate (line 2913)
//   TrailingComma(P, EndSet) <=> IsPunc(Tok(P), ",") && Tok(Advance(P)) in EndSet
//
// Detects trailing comma before end delimiter.

bool TrailingComma(const Parser& parser, std::span<const EndSetToken> end_set) {
  const Token* tok = Tok(parser);
  if (!tok || tok->kind != TokenKind::Punctuator || tok->lexeme != ",") {
    return false;
  }
  const Parser next = AdvanceOrEOF(parser);
  const Token* next_tok = Tok(next);
  if (!next_tok) {
    return false;
  }
  return TokenInEndSet(*next_tok, end_set);
}

// =============================================================================
// TrailingCommaAllowed - Check if trailing comma is valid
// =============================================================================
//
// SPEC: TrailingCommaAllowed predicate (line 2917)
//   TrailingCommaAllowed(P_0, P, EndSet) <=>
//     TrailingComma(P, EndSet) && TokLine(Tok(P)) < TokLine(Tok(Advance(P)))
//
// Trailing comma is allowed only when closing delimiter is on different line.

bool TrailingCommaAllowed(const Parser& parser,
                          std::span<const EndSetToken> end_set) {
  if (!TrailingComma(parser, end_set)) {
    return false;
  }
  const Token* comma = Tok(parser);
  const Parser next = AdvanceOrEOF(parser);
  const Token* end_tok = Tok(next);
  if (!comma || !end_tok) {
    return false;
  }
  return comma->span.start_line < end_tok->span.start_line;
}

// =============================================================================
// EmitTrailingCommaErr - Emit error for invalid trailing comma
// =============================================================================
//
// SPEC: Trailing-Comma-Err (lines 2919-2921)
//   TrailingComma && !TrailingCommaAllowed => Emit(Code(Trailing-Comma-Err))
//
// Emits E-SRC-0521 diagnostic for single-line trailing commas.

bool EmitTrailingCommaErr(Parser& parser,
                          std::span<const EndSetToken> end_set) {
  const std::optional<InvalidTrailingCommaEmitState> invalid =
      InvalidTrailingCommaForEmit(parser, end_set);
  if (!invalid) {
    return false;
  }
  SPEC_RULE("Trailing-Comma-Err");
  auto diag = core::MakeDiagnosticById("E-SRC-0521", invalid->comma->span);
  if (!diag) {
    return true;
  }
  core::Emit(parser.diags, *diag);
  return true;
}

}  // namespace ultraviolet::ast
