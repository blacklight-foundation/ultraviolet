// =============================================================================
// parser_paths.cpp - Path Parsing (Identifiers, Module Paths, Visibility)
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.6.1-3.3.6.2 (Lines 3468-3515)
//
// This file implements path and identifier parsing:
//   - ParseIdent: Parse single identifier
//   - ParseModulePath: Parse module path (a::b::c)
//   - ParseTypePath: Parse type path (a::b::C)
//   - ParseClassPath: Parse class path
//   - ParseQualifiedHead: Parse qualified name prefix (module::name)
//   - ParseVis: Parse visibility modifier
//   - ParseModalOpt: Parse optional "modal" keyword
//   - ParseAliasOpt: Parse optional "as alias" suffix
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

bool IsOp(const Parser& parser, std::string_view op);

namespace {

ExprPtr MakeExpr(const core::Span& span, ExprNode node) {
  auto expr = std::make_shared<Expr>();
  expr->span = span;
  expr->node = std::move(node);
  return expr;
}

bool IsIdentifierSlotToken(const Token* tok) {
  return tok && (IsIdentTok(*tok) || tok->kind == TokenKind::Keyword);
}

void RecordRestrictedSpliceIdentIfPresent(Parser parser) {
  if (!parser.quote_mode || !IsOp(parser, "$")) {
    return;
  }

  Parser after_dollar = parser;
  Advance(after_dollar);
  if (!IsIdentifierSlotToken(Tok(after_dollar))) {
    return;
  }

  Parser after_ident = after_dollar;
  Advance(after_ident);
  SPEC_RULE_AT("requirement.22.SpliceIdentifierPositionRestrictions",
               SpanBetween(parser, after_ident));
}

void EmitReservedKeywordIdentifierErr(Parser& parser, const core::Span& span) {
  auto diag = core::MakeDiagnosticById("E-CNF-0401", span);
  if (!diag) {
    return;
  }
  core::Emit(parser.diags, *diag);
}

struct QualifiedSegment {
  Identifier name;
  core::Span span;
  bool is_keyword = false;
};

ParseElemResult<QualifiedSegment> ParseQualifiedSegment(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && IsIdentifierSlotToken(tok)) {
    QualifiedSegment segment;
    segment.name = tok->lexeme;
    segment.span = tok->span;
    segment.is_keyword = tok->kind == TokenKind::Keyword;
    Advance(parser);
    return {parser, std::move(segment)};
  }

  RecordRestrictedSpliceIdentIfPresent(parser);
  SPEC_RULE("Parse-Ident-Err");
  EmitGenericParseSyntaxErr(parser, TokSpan(parser));
  QualifiedSegment segment;
  segment.name = "_";
  segment.span = TokSpan(parser);
  return {parser, std::move(segment)};
}

bool IsSpecDefinedKeywordQualifiedName(
    const std::vector<QualifiedSegment>& segments,
    std::size_t index) {
  if (segments.size() == 2 && index == 1 &&
      segments[0].name == "string" && segments[1].name == "from") {
    return true;
  }
  return false;
}

void EmitReservedQualifiedSegmentErrs(
    Parser& parser,
    const std::vector<QualifiedSegment>& segments) {
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (!segments[i].is_keyword) {
      continue;
    }
    if (IsSpecDefinedKeywordQualifiedName(segments, i)) {
      continue;
    }
    EmitReservedKeywordIdentifierErr(parser, segments[i].span);
  }
}

// =============================================================================
// VisibilityFromLexeme - Convert a spec-defined visibility lexeme to enum
// =============================================================================

std::optional<Visibility> VisibilityFromLexeme(std::string_view lexeme) {
  if (lexeme == "public") {
    return Visibility::Public;
  }
  if (lexeme == "internal") {
    return Visibility::Internal;
  }
  if (lexeme == "private") {
    return Visibility::Private;
  }
  return std::nullopt;
}

// =============================================================================
// ParseModulePathTail - Recursive tail parsing for module paths
// =============================================================================
//
// SPEC: Parse-ModulePathTail-End (lines 3485-3488)
//   !IsOp(Tok(P), "::")
//   ----------------------------------------
//   ParseModulePathTail(P, xs) => (P, xs)
//
// SPEC: Parse-ModulePathTail-Cons (lines 3490-3493)
//   IsOp(Tok(P), "::")    ParseIdent(Advance(P)) => (P_1, id)
//   ParseModulePathTail(P_1, xs ++ [id]) => (P_2, ys)
//   ----------------------------------------
//   ParseModulePathTail(P, xs) => (P_2, ys)

ParseElemResult<ModulePath> ParseModulePathTail(Parser parser, ModulePath xs) {
  const Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "::")) {
    SPEC_RULE("Parse-ModulePathTail-End");
    return {parser, xs};
  }

  SPEC_RULE("Parse-ModulePathTail-Cons");
  Parser next = parser;
  Advance(next);
  ParseElemResult<Identifier> id = ParseIdent(next);
  xs.push_back(id.elem);
  return ParseModulePathTail(id.parser, std::move(xs));
}

// =============================================================================
// ParseTypePathTail - Recursive tail parsing for type paths
// =============================================================================
//
// SPEC: Parse-TypePathTail-End
//   !IsOp(Tok(P), "::")
//   ----------------------------------------
//   ParseTypePathTail(P, xs) => (P, xs)
//
// SPEC: Parse-TypePathTail-Cons
//   IsOp(Tok(P), "::")    ParseIdent(Advance(P)) => (P_1, id)
//   ParseTypePathTail(P_1, xs ++ [id]) => (P_2, ys)
//   ----------------------------------------
//   ParseTypePathTail(P, xs) => (P_2, ys)

ParseElemResult<TypePath> ParseTypePathTail(Parser parser, TypePath xs) {
  const Token* tok = Tok(parser);
  if (!tok || !IsOpTok(*tok, "::")) {
    SPEC_RULE("Parse-TypePathTail-End");
    return {parser, xs};
  }

  SPEC_RULE("Parse-TypePathTail-Cons");
  Parser next = parser;
  Advance(next);
  ParseElemResult<Identifier> id = ParseIdent(next);
  xs.push_back(id.elem);
  return ParseTypePathTail(id.parser, std::move(xs));
}

}  // namespace

// =============================================================================
// ParseIdent - Parse single identifier
// =============================================================================
//
// SPEC: Parse-Ident (lines 3470-3473)
//   IsIdent(Tok(P))
//   ----------------------------------------
//   ParseIdent(P) => (Advance(P), Lexeme(Tok(P)))
//
// SPEC: Parse-Ident-Err (lines 3475-3478)
//   !IsIdent(Tok(P))    c = Code(Parse-Syntax-Err)    Emit(c, Tok(P).span)
//   ----------------------------------------
//   ParseIdent(P) => (P, "_")

ParseElemResult<Identifier> ParseIdent(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && IsIdentTok(*tok)) {
    SPEC_RULE("Parse-Ident");
    Identifier name = tok->lexeme;
    Advance(parser);
    return {parser, name};
  }

  if (tok && tok->kind == TokenKind::Keyword) {
    Identifier name = tok->lexeme;
    EmitReservedKeywordIdentifierErr(parser, tok->span);
    Advance(parser);
    return {parser, name};
  }

  RecordRestrictedSpliceIdentIfPresent(parser);
  SPEC_RULE("Parse-Ident-Err");
  EmitGenericParseSyntaxErr(parser, TokSpan(parser));
  return {parser, Identifier{"_"}};
}

ParseLocalIdentResult ParseLocalIdent(Parser parser) {
  if (IsOp(parser, "$")) {
    if (!parser.quote_mode) {
      Parser after_dollar = parser;
      Advance(after_dollar);
      const Token* tok = Tok(after_dollar);
      if (IsIdentifierSlotToken(tok)) {
        Parser after_ident = after_dollar;
        if (tok->kind == TokenKind::Keyword) {
          EmitReservedKeywordIdentifierErr(after_ident, tok->span);
        }
        Advance(after_ident);
        EmitSpliceOutsideQuoteErr(after_ident, SpanBetween(parser, after_ident));
        return {after_ident, Identifier{"_"}, std::nullopt};
      }
    }
  }

  if (parser.quote_mode && IsOp(parser, "$")) {
    Parser after_dollar = parser;
    Advance(after_dollar);
    const Token* tok = Tok(after_dollar);
    if (!IsIdentifierSlotToken(tok)) {
      EmitParseSyntaxErr(after_dollar, TokSpan(after_dollar));
      return {after_dollar, Identifier{"_"}, std::nullopt};
    }

    Parser after_ident = after_dollar;
    if (tok->kind == TokenKind::Keyword) {
      EmitReservedKeywordIdentifierErr(after_ident, tok->span);
    }
    Advance(after_ident);

    IdentifierExpr ident;
    ident.name = tok->lexeme;

    SpliceIdentNode splice;
    splice.name_expr = MakeExpr(tok->span, ident);
    splice.span = SpanBetween(parser, after_ident);
    return {after_ident, Identifier{"_"}, std::move(splice)};
  }

  ParseElemResult<Identifier> ident = ParseIdent(parser);
  return {ident.parser, std::move(ident.elem), std::nullopt};
}

// =============================================================================
// ParseModulePath - Parse module path (a::b::c)
// =============================================================================
//
// SPEC: Parse-ModulePath (lines 3480-3483)
//   ParseIdent(P) => (P_1, id)    ParseModulePathTail(P_1, [id]) => (P_2, path)
//   ----------------------------------------
//   ParseModulePath(P) => (P_2, path)

ParseElemResult<ModulePath> ParseModulePath(Parser parser) {
  SPEC_RULE("Parse-ModulePath");
  ParseElemResult<Identifier> head = ParseIdent(parser);
  ModulePath path;
  path.push_back(head.elem);
  return ParseModulePathTail(head.parser, std::move(path));
}

// =============================================================================
// ParseTypePath - Parse type path (a::b::C)
// =============================================================================
//
// SPEC: Parse-TypePath

ParseElemResult<TypePath> ParseTypePath(Parser parser) {
  SPEC_RULE("Parse-TypePath");
  ParseElemResult<Identifier> head = ParseIdent(parser);
  TypePath path;
  path.push_back(head.elem);
  return ParseTypePathTail(head.parser, std::move(path));
}

// =============================================================================
// ParseClassPath - Parse class path
// =============================================================================
//
// SPEC: Parse-ClassPath

ParseElemResult<ClassPath> ParseClassPath(Parser parser) {
  SPEC_RULE("Parse-ClassPath");
  ParseElemResult<TypePath> path = ParseTypePath(parser);
  return {path.parser, path.elem};
}

// =============================================================================
// ParseQualifiedHead - Parse qualified name prefix (module::name)
// =============================================================================
//
// SPEC: Parse-QualifiedHead
// Parses qualified name like `module::path::name` and splits into
// module prefix and final name component.

ParseQualifiedHeadResult ParseQualifiedHead(Parser parser) {
  SPEC_RULE("Parse-QualifiedHead");
  std::vector<QualifiedSegment> segments;
  ParseElemResult<QualifiedSegment> head = ParseQualifiedSegment(parser);
  segments.push_back(std::move(head.elem));

  const Token* tok = Tok(head.parser);
  if (!tok || !IsOpTok(*tok, "::")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, {}, "_"};
  }

  Parser cur = head.parser;
  while (const Token* sep = Tok(cur)) {
    if (!IsOpTok(*sep, "::")) {
      break;
    }
    Advance(cur);
    ParseElemResult<QualifiedSegment> segment = ParseQualifiedSegment(cur);
    segments.push_back(std::move(segment.elem));
    cur = segment.parser;
  }

  if (segments.size() < 2) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {cur, {}, "_"};
  }

  EmitReservedQualifiedSegmentErrs(cur, segments);
  ModulePath full;
  full.reserve(segments.size());
  for (const auto& segment : segments) {
    full.push_back(segment.name);
  }
  Identifier name = full.back();
  full.pop_back();
  return {cur, full, name};
}

// =============================================================================
// ParseVis - Parse visibility modifier
// =============================================================================
//
// SPEC: Parse-Vis-Opt (lines 3497-3500)
//   IsKw(Tok(P), v)    v in {public, internal, private}
//   ----------------------------------------
//   ParseVis(P) => (Advance(P), v)
//
// SPEC: Parse-Vis-Default (lines 3502-3505)
//   !IsKw(Tok(P), v)
//   ----------------------------------------
//   ParseVis(P) => (P, internal)

ParseElemResult<Visibility> ParseVis(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && tok->kind == TokenKind::Keyword) {
    if (std::optional<Visibility> vis = VisibilityFromLexeme(tok->lexeme)) {
      SPEC_RULE("Parse-Vis-Opt");
      Advance(parser);
      return {parser, *vis};
    }
  }

  SPEC_RULE("Parse-Vis-Default");
  return {parser, Visibility::Internal};
}

// =============================================================================
// ParseKeyBoundaryOpt - Parse optional '#' key-boundary marker
// =============================================================================
//
// SPEC: Parse-KeyBoundaryOpt-Yes
//   IsOp(Tok(P), "#")
//   ----------------------------------------
//   ParseKeyBoundaryOpt(P) => (Advance(P), true)
//
// SPEC: Parse-KeyBoundaryOpt-No
//   !IsOp(Tok(P), "#")
//   ----------------------------------------
//   ParseKeyBoundaryOpt(P) => (P, false)

ParseElemResult<bool> ParseKeyBoundaryOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && IsOpTok(*tok, "#")) {
    SPEC_RULE("Parse-KeyBoundaryOpt-Yes");
    Advance(parser);
    return {parser, true};
  }

  SPEC_RULE("Parse-KeyBoundaryOpt-No");
  return {parser, false};
}

// =============================================================================
// ParseModalOpt - Parse optional "modal" keyword
// =============================================================================
//
// SPEC: Parse-ModalOpt-Yes
//   IsKw(Tok(P), "modal")
//   ----------------------------------------
//   ParseModalOpt(P) => (Advance(P), true)
//
// SPEC: Parse-ModalOpt-No
//   !IsKw(Tok(P), "modal")
//   ----------------------------------------
//   ParseModalOpt(P) => (P, false)

ParseElemResult<bool> ParseModalOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && IsKwTok(*tok, "modal")) {
    SPEC_RULE("Parse-ModalOpt-Yes");
    Advance(parser);
    return {parser, true};
  }

  SPEC_RULE("Parse-ModalOpt-No");
  return {parser, false};
}

// =============================================================================
// ParseAliasOpt - Parse optional "as alias" suffix
// =============================================================================
//
// SPEC: Parse-AliasOpt-Yes
//   IsKw(Tok(P), "as")
//   ----------------------------------------
//   ParseAliasOpt(P) => (ParseIdent(Advance(P)), id)
//
// SPEC: Parse-AliasOpt-None
//   !IsKw(Tok(P), "as")
//   ----------------------------------------
//   ParseAliasOpt(P) => (P, nullopt)

ParseElemResult<std::optional<Identifier>> ParseAliasOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (tok && IsKwTok(*tok, "as")) {
    SPEC_RULE("Parse-AliasOpt-Yes");
    Parser next = parser;
    Advance(next);
    ParseElemResult<Identifier> id = ParseIdent(next);
    return {id.parser, id.elem};
  }

  SPEC_RULE("Parse-AliasOpt-None");
  return {parser, std::nullopt};
}

}  // namespace ultraviolet::ast
