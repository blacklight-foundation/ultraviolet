// =============================================================================
// parser_terminator.cpp - Statement Terminator Handling
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.2.3 - Statement Termination
//
// Cursive uses newline OR semicolon as statement terminators.
// Both are valid; semicolon allows multiple statements per line.
//
// This file implements:
//   - ConsumeTerminatorOpt: Consume terminator with configurable policy
//   - ConsumeTerminatorReq: Always require terminator
//
// =============================================================================

#include "02_source/parser/parser.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

namespace {

// =============================================================================
// IsTerminatorToken - Check if token is a statement terminator
// =============================================================================
//
// Statement terminators are Newline and ";"

bool IsTerminatorToken(const Token& tok) {
  return tok.kind == TokenKind::Newline ||
         (tok.kind == TokenKind::Punctuator && tok.lexeme == ";");
}

// =============================================================================
// EmitMissingTerminator - Emit missing terminator diagnostic
// =============================================================================

void EmitMissingTerminator(core::DiagnosticStream& diags, const core::Span& span) {
  auto diag = core::MakeDiagnosticById("E-SRC-0510", span);
  if (!diag) {
    return;
  }
  core::Emit(diags, *diag);
}

}  // namespace

// =============================================================================
// ConsumeTerminatorOpt - Consume terminator with configurable policy
// =============================================================================
//
// SPEC RULE markers:
//   - "ConsumeTerminatorOpt-Req-Yes": Required policy, terminator present
//   - "ConsumeTerminatorOpt-Req-No": Required policy, terminator missing
//   - "ConsumeTerminatorOpt-Opt-Yes": Optional policy, terminator present
//   - "ConsumeTerminatorOpt-Opt-No": Optional policy, terminator missing

void ConsumeTerminatorOpt(Parser& parser, TerminatorPolicy policy) {
  const Token* tok = Tok(parser);
  const bool is_term = tok && IsTerminatorToken(*tok);

  if (policy == TerminatorPolicy::Required) {
    if (is_term) {
      SPEC_RULE("ConsumeTerminatorOpt-Req-Yes");
      Advance(parser);
      return;
    }
    SPEC_RULE("ConsumeTerminatorOpt-Req-No");
    EmitMissingTerminator(parser.diags, TokSpan(parser));
    SyncStmt(parser);
    return;
  }

  if (is_term) {
    SPEC_RULE("ConsumeTerminatorOpt-Opt-Yes");
    Advance(parser);
  } else {
    SPEC_RULE("ConsumeTerminatorOpt-Opt-No");
  }
}

// =============================================================================
// ConsumeTerminatorReq - Always require terminator
// =============================================================================
//
// SPEC RULE markers:
//   - "ConsumeTerminatorReq-Yes": Terminator present
//   - "ConsumeTerminatorReq-No": Terminator missing

void ConsumeTerminatorReq(Parser& parser) {
  const Token* tok = Tok(parser);
  if (tok && IsTerminatorToken(*tok)) {
    SPEC_RULE("ConsumeTerminatorReq-Yes");
    Advance(parser);
    return;
  }
  SPEC_RULE("ConsumeTerminatorReq-No");
  EmitMissingTerminator(parser.diags, TokSpan(parser));
  SyncStmt(parser);
}

}  // namespace cursive::ast
