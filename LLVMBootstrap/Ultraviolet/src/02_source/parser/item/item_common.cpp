// =============================================================================
// item_common.cpp - Common Helper Functions for Item Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.6.13 (Item Helper Parsing Rules)
//
// This file implements common helper functions used across all item parsers:
//   - SkipNewlines: Skip consecutive newline tokens
//   - IsWhereTok: Check for contextual "where" keyword
//   - IsKw, IsOp, IsPunc: Token kind predicates
//   - EmitUnsupportedConstruct: Emit a syntax diagnostic
//   - MakeErrorType: Create error type for recovery
//   - NormalizeBindingPattern: Normalize typed patterns
//   - EmitImportUnsupported: Emit a syntax diagnostic
//   - EmitExternUnsupported: Emit a syntax diagnostic
//   - EmitReturnAtModuleErr: Emit E-SEM-3165 diagnostic
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <string_view>

#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;
using ultraviolet::lexer::IsIdentTok;
using ultraviolet::lexer::IsKwTok;
using ultraviolet::lexer::IsOpTok;
using ultraviolet::lexer::IsPuncTok;

// =============================================================================
// SkipNewlines - Skip consecutive newline tokens
// =============================================================================
//
// Consumes all consecutive newline tokens from the parser position.
// Used before many item components to skip whitespace.

void SkipNewlines(Parser& parser) {
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }
}

// =============================================================================
// IsWhereTok - Check for contextual "where" keyword
// =============================================================================
//
// "where" is a contextual keyword (parsed as identifier).
// Used to detect start of where clause vs type invariant.

bool IsWhereTok(const Parser& parser) {
  const Token* tok = Tok(parser);
  return tok && IsIdentTok(*tok) && tok->lexeme == "where";
}

// =============================================================================
// IsKw - Check if current token is a specific keyword
// =============================================================================

bool IsKw(const Parser& parser, std::string_view kw) {
  const Token* tok = Tok(parser);
  return tok && IsKwTok(*tok, kw);
}

// =============================================================================
// IsOp - Check if current token is a specific operator
// =============================================================================

bool IsOp(const Parser& parser, std::string_view op) {
  const Token* tok = Tok(parser);
  return tok && IsOpTok(*tok, op);
}

// =============================================================================
// IsPunc - Check if current token is a specific punctuator
// =============================================================================
//
// Handles multi-char operators such as "::"; attribute delimiters are adjacent
// punctuator pairs consumed by the attribute parser.

bool IsPunc(const Parser& parser, std::string_view p) {
  const Token* tok = Tok(parser);
  return tok && IsPuncTok(*tok, p);
}

// =============================================================================
// EmitUnsupportedConstruct - Emit a syntax diagnostic
// =============================================================================
//
// Emits a parse diagnostic for forms that do not match the implemented grammar.

void EmitUnsupportedConstruct(Parser& parser) {
  EmitParseSyntaxErr(parser, TokSpan(parser));
}

// =============================================================================
// MakeErrorType - Create error type for recovery
// =============================================================================
//
// Creates a "never" type (!) for error recovery when parsing fails
// but we need a valid Type node.

std::shared_ptr<Type> MakeErrorType(const core::Span& span) {
  auto ty = std::make_shared<Type>();
  ty->span = span;
  ty->node = TypePrim{Identifier{"!"}};
  return ty;
}

// =============================================================================
// NormalizeBindingPattern - Normalize typed patterns
// =============================================================================
//
// Normalizes binding patterns like "x: T" into separate pattern and type.
// Handles the case where type annotation is part of pattern vs separate.
// Important for consistent AST representation.

void NormalizeBindingPattern(std::shared_ptr<Pattern>& pat,
                             std::shared_ptr<Type>& type_opt) {
  if (!pat || type_opt) {
    return;
  }
  const auto* typed = std::get_if<TypedPattern>(&pat->node);
  if (!typed) {
    return;
  }
  auto normalized = std::make_shared<Pattern>();
  normalized->span = pat->span;
  normalized->node = IdentifierPattern{typed->name};
  type_opt = typed->type;
  pat = std::move(normalized);
}

// =============================================================================
// EmitImportUnsupported - Emit a syntax diagnostic
// =============================================================================
//
// SPEC RULE: WF-Import-Unsupported

void EmitImportUnsupported(Parser& parser) {
  SPEC_RULE("WF-Import-Unsupported");
  EmitParseSyntaxErr(parser, TokSpan(parser));
}

// =============================================================================
// EmitExternUnsupported - Emit a syntax diagnostic
// =============================================================================

void EmitExternUnsupported(Parser& parser) {
  EmitParseSyntaxErr(parser, TokSpan(parser));
}

// =============================================================================
// EmitReturnAtModuleErr - Emit E-SEM-3165 diagnostic
// =============================================================================

void EmitReturnAtModuleErr(Parser& parser) {
  if (parser.quote_mode) {
    return;
  }
  auto diag = core::MakeDiagnosticById("E-SEM-3165");
  if (!diag) {
    return;
  }
  core::Emit(parser.diags, *diag);
}

}  // namespace ultraviolet::ast
