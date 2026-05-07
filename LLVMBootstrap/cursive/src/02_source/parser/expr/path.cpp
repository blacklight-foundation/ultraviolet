// =============================================================================
// path.cpp - Qualified Path Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Parse-Qualified-Name (Lines 5189-5192)
// - Parse-Qualified-Apply-Paren (Lines 5194-5197)
// - Parse-Qualified-Apply-Brace (Lines 5199-5202)
// - Generic qualifier rejection for path::name<T> style misuse
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Import token inspection functions from lexer
using lexer::IsIdentTok;
using lexer::IsOpTok;
using lexer::IsPuncTok;

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
bool IsPunc(const Parser& parser, std::string_view punc);
Parser SkipAngles(const Parser& parser);
ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser);
ParseElemResult<std::vector<FieldInit>> ParseFieldInitList(Parser parser);

// =============================================================================
// ParseQualifiedApplyParen - Parse path::name(args)
// =============================================================================
//
// SPEC: Lines 5194-5197
// Assumes head has been parsed and current token is "(".

ParseElemResult<ExprPtr> ParseQualifiedApplyParen(
    Parser original_parser, const ParseQualifiedHeadResult& head) {
  SPEC_RULE("Parse-Qualified-Apply-Paren");
  Parser after_l = head.parser;
  Advance(after_l);  // consume "("
  ParseElemResult<std::vector<Arg>> args = ParseArgList(after_l);
  if (!IsPunc(args.parser, ")")) {
    EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
    Parser sync = args.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(original_parser, sync), ErrorExpr{})};
  }
  Parser after = args.parser;
  Advance(after);  // consume ")"
  QualifiedApplyExpr app;
  app.path = head.module_path;
  app.name = head.name;
  app.args = ParenArgs{std::move(args.elem)};
  return {after, MakeExpr(SpanBetween(original_parser, after), app)};
}

// =============================================================================
// ParseQualifiedApplyBrace - Parse path::name{fields}
// =============================================================================
//
// SPEC: Lines 5199-5202
// Assumes head has been parsed and current token is "{".
// Only called when allow_brace=true.

ParseElemResult<ExprPtr> ParseQualifiedApplyBrace(
    Parser original_parser, const ParseQualifiedHeadResult& head) {
  SPEC_RULE("Parse-Qualified-Apply-Brace");
  Parser after_l = head.parser;
  Advance(after_l);  // consume "{"
  ParseElemResult<std::vector<FieldInit>> fields = ParseFieldInitList(after_l);
  if (fields.elem.empty() && IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser after = fields.parser;
    Advance(after);  // consume "}"
    return {after, MakeExpr(SpanBetween(original_parser, after), ErrorExpr{})};
  }
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser sync = fields.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(original_parser, sync), ErrorExpr{})};
  }
  Parser after = fields.parser;
  Advance(after);  // consume "}"
  QualifiedApplyExpr app;
  app.path = head.module_path;
  app.name = head.name;
  app.args = BraceArgs{std::move(fields.elem)};
  return {after, MakeExpr(SpanBetween(original_parser, after), app)};
}

// =============================================================================
// ParseQualifiedNameExpr - Parse path::name (no call/brace)
// =============================================================================
//
// SPEC: Lines 5189-5192
// Assumes head has been parsed and no "(" or "{" or "@" follows.

ParseElemResult<ExprPtr> ParseQualifiedNameExpr(
    Parser original_parser, const ParseQualifiedHeadResult& head) {
  SPEC_RULE("Parse-Qualified-Name");
  QualifiedNameExpr qname;
  qname.path = head.module_path;
  qname.name = head.name;
  return {head.parser, MakeExpr(SpanBetween(original_parser, head.parser), qname)};
}

// =============================================================================
// TryParseQualifiedExpr - Try to parse qualified expressions
// =============================================================================
//
// Entry point for qualified path expressions. Called when an identifier
// is followed by "::". Dispatches to:
// - QualifiedApply with paren (path::name(args))
// - QualifiedApply with brace (path::name{fields}) if allow_brace=true
// - QualifiedName (path::name) otherwise
//
// Also rejects generic qualifiers in this position with a syntax error.
//
// Returns std::nullopt if:
// - Current token is not an identifier, OR
// - Next token is not "::"
// Returns the appropriate qualified expression otherwise.

std::optional<ParseElemResult<ExprPtr>> TryParseQualifiedExpr(
    Parser parser, bool allow_brace) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    return std::nullopt;
  }

  Parser next = parser;
  Advance(next);
  const lexer::Token* look = Tok(next);
  if (!look || !IsOpTok(*look, "::")) {
    return std::nullopt;
  }

  // Parse the qualified head (path::name)
  ParseQualifiedHeadResult head = ParseQualifiedHead(parser);
  const lexer::Token* after_head = Tok(head.parser);

  // Check for unsupported generic qualifiers (path<T>::name)
  if (after_head && IsOpTok(*after_head, "<")) {
    EmitParseSyntaxErr(head.parser, TokSpan(head.parser));
    Parser skip = SkipAngles(head.parser);
    SyncStmt(skip);
    return ParseElemResult<ExprPtr>{
        skip, MakeExpr(SpanBetween(parser, skip), ErrorExpr{})};
  }

  // Check for QualifiedApply with parentheses
  if (after_head && IsPuncTok(*after_head, "(")) {
    return ParseQualifiedApplyParen(parser, head);
  }

  // Check for QualifiedApply with braces (only if allow_brace)
  if (allow_brace && after_head && IsPuncTok(*after_head, "{")) {
    return ParseQualifiedApplyBrace(parser, head);
  }

  // Check that it's not followed by "@" (modal state reference)
  // If followed by "@", return nullopt to let modal parsing handle it
  if (after_head && after_head->kind == lexer::TokenKind::Operator &&
      after_head->lexeme == "@") {
    return std::nullopt;
  }

  // Default: QualifiedName (path::name with no call)
  return ParseQualifiedNameExpr(parser, head);
}

}  // namespace cursive::ast
