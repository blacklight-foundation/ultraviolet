// =============================================================================
// parser_recovery.cpp - Error Recovery and Synchronization
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.12 (Lines 6452-6508)
//
// This file implements error recovery operations:
//   - SyncStmt: Synchronize to next statement boundary
//   - SyncItem: Synchronize to next item boundary
//   - SyncType: Synchronize to next type boundary
//   - EmitParseSyntaxErr: Emit syntax error diagnostic
//
// Synchronization sets:
//   - SyncStmt: {";", "}", EOF}
//   - SyncItem: {procedure, record, enum, modal, class, type, using, let, var, "}", EOF}
//   - SyncType: {",", ";", Newline, ")", "]", "}", EOF}
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <string_view>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

namespace {

static inline void SpecDefsParserRecovery() {
  SPEC_DEF("StmtParseErrRule", "5.8");
  SPEC_DEF("ItemParseErrRule", "5.8");
  SPEC_DEF("Phase1DiagRules", "5.9");
}

// =============================================================================
// IsSyncItemToken - Check if token is in item synchronization set
// =============================================================================
//
// SPEC: Section 3.3.12 lines 6458-6460
//   SyncItem = {Keyword(procedure), Keyword(record), Keyword(enum), Keyword(modal),
//               Keyword(class), Keyword(type), Keyword(using), Keyword(let),
//               Keyword(var), Punctuator("}"), EOF}

bool IsSyncItemToken(const Token& tok) {
  if (tok.kind == TokenKind::Keyword) {
    const std::string_view lex = tok.lexeme;
    return lex == "procedure" || lex == "record" || lex == "enum" ||
           lex == "modal" || lex == "class" || lex == "type" ||
           lex == "using" || lex == "let" || lex == "var";
  }
  return tok.kind == TokenKind::Punctuator && tok.lexeme == "}";
}

// =============================================================================
// IsTerminatorToken - Check if token is a statement terminator
// =============================================================================
//
// SPEC: ParseRecovery sync set for statements: {";", "}", EOF}

bool IsTerminatorToken(const Token& tok) {
  return tok.kind == TokenKind::Punctuator && tok.lexeme == ";";
}

}  // namespace

// =============================================================================
// EmitParseSyntaxErr - Emit syntax error diagnostic
// =============================================================================
//
// Emits E-SRC-0520 diagnostic for syntax errors without claiming a specific
// spec rule. Generic parse rules that normatively emit Parse-Syntax-Err must
// use EmitGenericParseSyntaxErr instead.

void EmitParseSyntaxErr(Parser& parser, const core::Span& span) {
  if (parser.quote_mode) {
    return;
  }
  auto diag = core::MakeDiagnosticById("E-SRC-0520", span);
  if (!diag) {
    return;
  }
  core::Emit(parser.diags, *diag);
}

// =============================================================================
// EmitGenericParseSyntaxErr - Emit generic parse syntax diagnostic
// =============================================================================
//
// SPEC: Parse-Syntax-Err (Section 3.3.13 lines 6513-6518)
//   GenericParseRules = {Parse-Ident-Err, Parse-Type-Err, Parse-Pattern-Err,
//                        Parse-Primary-Err, Parse-Statement-Err, Parse-Item-Err}
//   r in GenericParseRules    PremisesHold(r, P)
//   ----------------------------------------
//   Emit(Code(Parse-Syntax-Err))

void EmitGenericParseSyntaxErr(Parser& parser, const core::Span& span) {
  if (parser.quote_mode) {
    return;
  }
  SpecDefsParserRecovery();
  SPEC_RULE("Parse-Syntax-Err");
  EmitParseSyntaxErr(parser, span);
}

void EmitSpliceOutsideQuoteErr(Parser& parser, const core::Span& span) {
  auto diag = core::MakeDiagnosticById("E-CTE-0233", span);
  if (!diag) {
    return;
  }
  core::Emit(parser.diags, *diag);
}

// =============================================================================
// SyncStmt - Synchronize to next statement boundary
// =============================================================================
//
// SPEC: Section 3.3.12 lines 6466-6479
//
// (Sync-Stmt-Stop) - lines 6466-6469:
//   Tok(P) in {Punctuator("}"), EOF}
//   ----------------------------------------
//   SyncStmt(P) => P
//
// (Sync-Stmt-Consume) - lines 6471-6474:
//   Tok(P) in {Punctuator(";"), Newline}
//   ----------------------------------------
//   SyncStmt(P) => Advance(P)
//
// (Sync-Stmt-Advance) - lines 6476-6479:
//   Tok(P) not in SyncStmt
//   ----------------------------------------
//   SyncStmt(P) => SyncStmt(Advance(P))

void SyncStmt(Parser& parser) {
  for (;;) {
    if (AtEof(parser)) {
      SPEC_RULE("Sync-Stmt-Stop");
      return;
    }
    const Token* tok = Tok(parser);
    if (!tok) {
      SPEC_RULE("Sync-Stmt-Stop");
      return;
    }
    if (tok->kind == TokenKind::Punctuator && tok->lexeme == "}") {
      SPEC_RULE("Sync-Stmt-Stop");
      return;
    }
    if (IsTerminatorToken(*tok)) {
      SPEC_RULE("Sync-Stmt-Consume");
      Advance(parser);
      return;
    }
    SPEC_RULE("Sync-Stmt-Advance");
    Advance(parser);
  }
}

// =============================================================================
// SyncItem - Synchronize to next item boundary
// =============================================================================
//
// SPEC: Section 3.3.12 lines 6481-6489
//
// (Sync-Item-Stop) - lines 6481-6484:
//   Tok(P) in SyncItem
//   ----------------------------------------
//   SyncItem(P) => P
//
// (Sync-Item-Advance) - lines 6486-6489:
//   Tok(P) not in SyncItem
//   ----------------------------------------
//   SyncItem(P) => SyncItem(Advance(P))

void SyncItem(Parser& parser) {
  for (;;) {
    if (AtEof(parser)) {
      SPEC_RULE("Sync-Item-Stop");
      return;
    }
    const Token* tok = Tok(parser);
    if (!tok) {
      SPEC_RULE("Sync-Item-Stop");
      return;
    }
    if (IsSyncItemToken(*tok)) {
      SPEC_RULE("Sync-Item-Stop");
      return;
    }
    SPEC_RULE("Sync-Item-Advance");
    Advance(parser);
  }
}

// =============================================================================
// SyncType - Synchronize to next type boundary
// =============================================================================
//
// SPEC: Section 3.3.12 lines 6491-6504
//
// (Sync-Type-Stop) - lines 6491-6494:
//   Tok(P) in {Punctuator(")"), Punctuator("]"), Punctuator("}"), EOF}
//   ----------------------------------------
//   SyncType(P) => P
//
// (Sync-Type-Consume) - lines 6496-6499:
//   Tok(P) in {Punctuator(","), Punctuator(";"), Newline}
//   ----------------------------------------
//   SyncType(P) => Advance(P)
//
// (Sync-Type-Advance) - lines 6501-6504:
//   Tok(P) not in SyncType
//   ----------------------------------------
//   SyncType(P) => SyncType(Advance(P))

void SyncType(Parser& parser) {
  for (;;) {
    if (AtEof(parser)) {
      SPEC_RULE("Sync-Type-Stop");
      return;
    }
    const Token* tok = Tok(parser);
    if (!tok) {
      SPEC_RULE("Sync-Type-Stop");
      return;
    }
    if (tok->kind == TokenKind::Punctuator) {
      if (tok->lexeme == ")" || tok->lexeme == "]" || tok->lexeme == "}") {
        SPEC_RULE("Sync-Type-Stop");
        return;
      }
      if (tok->lexeme == "," || tok->lexeme == ";") {
        SPEC_RULE("Sync-Type-Consume");
        Advance(parser);
        return;
      }
    }
    if (tok->kind == TokenKind::Newline) {
      SPEC_RULE("Sync-Type-Consume");
      Advance(parser);
      return;
    }
    SPEC_RULE("Sync-Type-Advance");
    Advance(parser);
  }
}

}  // namespace cursive::ast
