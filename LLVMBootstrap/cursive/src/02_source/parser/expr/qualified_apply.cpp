// =============================================================================
// qualified_apply.cpp - Qualified Apply Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Parse-Qualified-Apply-Paren (Lines 5194-5197)
// - Parse-Qualified-Apply-Brace (Lines 5199-5202)
//
// This file provides the main entry point ParseQualifiedApply which is called
// from ParsePrimary when an identifier is encountered that might start a
// qualified path expression (path::name(...) or path::name{...}).
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
// ParseQualifiedApply - Main entry point for qualified expressions
// =============================================================================
//
// SPEC: Lines 5194-5202
// Called when we need to parse a qualified path expression.
// Dispatches to:
// - QualifiedApply with paren (path::name(args))
// - QualifiedApply with brace (path::name{fields}) if allow_brace=true
// - QualifiedName (path::name) otherwise
//
// Also rejects generic qualifiers in this position with a syntax error.

ParseElemResult<ExprPtr> ParseQualifiedApply(Parser parser, bool allow_brace) {
  const lexer::Token* tok = Tok(parser);
  if (!tok || !IsIdentTok(*tok)) {
    // Not an identifier - emit error and return error expression
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, MakeExpr(TokSpan(parser), ErrorExpr{})};
  }

  // Parse the qualified head (path::name)
  ParseQualifiedHeadResult head = ParseQualifiedHead(parser);
  const lexer::Token* after_head = Tok(head.parser);

  // Check for unsupported generic qualifiers (path<T>::name)
  if (after_head && IsOpTok(*after_head, "<")) {
    EmitParseSyntaxErr(head.parser, TokSpan(head.parser));
    Parser skip = SkipAngles(head.parser);
    SyncStmt(skip);
    return {skip, MakeExpr(SpanBetween(parser, skip), ErrorExpr{})};
  }

  // Check for QualifiedApply with parentheses
  if (after_head && IsPuncTok(*after_head, "(")) {
    SPEC_RULE("Parse-Qualified-Apply-Paren");
    Parser after_l = head.parser;
    Advance(after_l);  // consume "("
    ParseElemResult<std::vector<Arg>> args = ParseArgList(after_l);
    if (!IsPunc(args.parser, ")")) {
      EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
      Parser sync = args.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after = args.parser;
    Advance(after);  // consume ")"
    QualifiedApplyExpr app;
    app.path = head.module_path;
    app.name = head.name;
    app.args = ParenArgs{std::move(args.elem)};
    return {after, MakeExpr(SpanBetween(parser, after), app)};
  }

  // Check for QualifiedApply with braces (only if allow_brace)
  if (allow_brace && after_head && IsPuncTok(*after_head, "{")) {
    SPEC_RULE("Parse-Qualified-Apply-Brace");
    Parser after_l = head.parser;
    Advance(after_l);  // consume "{"
    ParseElemResult<std::vector<FieldInit>> fields = ParseFieldInitList(after_l);
    if (fields.elem.empty() && IsPunc(fields.parser, "}")) {
      EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
      Parser after = fields.parser;
      Advance(after);  // consume "}"
      return {after, MakeExpr(SpanBetween(parser, after), ErrorExpr{})};
    }
    if (!IsPunc(fields.parser, "}")) {
      EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
      Parser sync = fields.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after = fields.parser;
    Advance(after);  // consume "}"
    QualifiedApplyExpr app;
    app.path = head.module_path;
    app.name = head.name;
    app.args = BraceArgs{std::move(fields.elem)};
    return {after, MakeExpr(SpanBetween(parser, after), app)};
  }

  // Default: QualifiedName (path::name with no call)
  SPEC_RULE("Parse-Qualified-Name");
  QualifiedNameExpr qname;
  qname.path = head.module_path;
  qname.name = head.name;
  return {head.parser, MakeExpr(SpanBetween(parser, head.parser), qname)};
}

}  // namespace cursive::ast
