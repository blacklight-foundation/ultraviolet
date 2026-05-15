// =============================================================================
// postfix.cpp - Postfix Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.8.4, Lines 5126-5156
// - Parse-Postfix (Lines 5128-5131)
// - Parse-PostfixTail (Lines 5133-5142)
// - PostfixStep (Lines 5144-5156)
//
// This file implements:
// - ParsePostfix: Main entry point for postfix expression parsing
// - ParsePostfixTail: Iteratively applies postfix operators
// - PostfixStep: Single postfix operator application
//
// Postfix operators:
// - Field access: expr.field
// - Tuple access: expr.0
// - Index access: expr[index]
// - Method call: expr~>method(args)
// - Call: expr(args)
// - Propagate: expr?
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/numeric_literals.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::IsIdentTok;
using cursive::lexer::IsOpTok;
using cursive::lexer::IsPuncTok;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from expr_common.cpp
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPostfixStart(const Token& tok);

// Forward declarations from other modules
bool IsPunc(const Parser& parser, std::string_view punc);
bool IsOp(const Parser& parser, std::string_view op);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParsePrimary(Parser parser, bool allow_brace);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser);
ParseElemResult<Identifier> ParseIdent(Parser parser);
ParseElemResult<ExprPtr> ParsePipeline(Parser parser, bool allow_brace,
                                       bool allow_bracket);
bool CallTypeArgsStart(Parser parser);
ParseElemResult<ExprPtr> ParseCallTypeArgsStep(Parser parser, ExprPtr expr);

namespace {

bool IsParallelOptionName(const Token& tok) {
  if (tok.kind != TokenKind::Identifier && tok.kind != TokenKind::Keyword) {
    return false;
  }
  return tok.lexeme == "cancel" || tok.lexeme == "name" ||
         tok.lexeme == "workgroup" || tok.lexeme == "workgroups";
}

bool IsParallelOptionListStart(Parser parser) {
  if (!IsPunc(parser, "[")) {
    return false;
  }

  Advance(parser);
  SkipNewlines(parser);

  const Token* opt_name = Tok(parser);
  if (!opt_name || !IsParallelOptionName(*opt_name)) {
    return false;
  }

  Advance(parser);
  return IsPunc(parser, ":");
}

}  // namespace

// Forward declarations from individual postfix modules
std::optional<ParseElemResult<ExprPtr>> TryParseFieldAccess(Parser parser,
                                                             ExprPtr base);
ParseElemResult<ExprPtr> ParseFieldAccessError(Parser parser, ExprPtr base);

// =============================================================================
// PostfixStep - Apply single postfix operator
// =============================================================================
//
// SPEC: Lines 5144-5156

ParseElemResult<ExprPtr> PostfixStep(Parser parser, ExprPtr expr,
                                      bool allow_bracket) {
  if (CallTypeArgsStart(parser)) {
    return ParseCallTypeArgsStep(parser, expr);
  }

  // Field or tuple access: expr.name or expr.0
  if (IsPunc(parser, ".")) {
    Parser next = parser;
    Advance(next);
    const Token* tok = Tok(next);

    // Field access: expr.name
    if (tok && (IsIdentTok(*tok) || tok->kind == TokenKind::Keyword)) {
      SPEC_RULE("Postfix-Field");
      Identifier name = tok->lexeme;
      Parser after = next;
      Advance(after);
      FieldAccessExpr field;
      field.base = expr;
      field.name = name;
      return {after, MakeExpr(SpanCover(expr->span, tok->span), field)};
    }

    // Tuple access: expr.0
    if (tok && tok->kind == TokenKind::IntLiteral) {
      const auto index_value =
          core::ParseIntCore(core::StripIntSuffix(tok->lexeme));
      if (index_value.has_value()) {
        SPEC_RULE("Postfix-TupleIndex");
        Parser after = next;
        Advance(after);
        TupleAccessExpr access;
        access.base = expr;
        access.index = *index_value;
        return {after, MakeExpr(SpanCover(expr->span, tok->span), access)};
      }
    }

    // Error: neither identifier nor integer after dot
    EmitParseSyntaxErr(next, TokSpan(next));
    Parser sync = next;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  // Index access: expr[index] (only when allow_bracket is true)
  if (IsPunc(parser, "[") && allow_bracket) {
    SPEC_RULE("Postfix-Index");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> index = ParseExpr(next);
    if (!IsPunc(index.parser, "]")) {
      EmitParseSyntaxErr(index.parser, TokSpan(index.parser));
      Parser sync = index.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    core::Span end_span = TokSpan(index.parser);
    Parser after = index.parser;
    Advance(after);
    IndexAccessExpr access;
    access.base = expr;
    access.index = index.elem;
    return {after, MakeExpr(SpanCover(expr->span, end_span), access)};
  }

  // Call: expr(args)
  if (IsPunc(parser, "(")) {
    SPEC_RULE("Postfix-Call");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::vector<Arg>> args = ParseArgList(next);
    if (!IsPunc(args.parser, ")")) {
      EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
      Parser sync = args.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    core::Span end_span = TokSpan(args.parser);
    Parser after = args.parser;
    Advance(after);

    // Build CallExpr with parsed arguments
    CallExpr call;
    call.callee = expr;
    call.args = std::move(args.elem);
    return {after, MakeExpr(SpanCover(expr->span, end_span), call)};
  }

  // Method call: expr~>method(args)
  if (IsOp(parser, "~>")) {
    SPEC_RULE("Postfix-MethodCall");
    Parser next = parser;
    Advance(next);
    ParseElemResult<Identifier> name = ParseIdent(next);
    if (!IsPunc(name.parser, "(")) {
      EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
      Parser sync = name.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    Parser after_lparen = name.parser;
    Advance(after_lparen);
    ParseElemResult<std::vector<Arg>> args = ParseArgList(after_lparen);
    if (!IsPunc(args.parser, ")")) {
      EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
      Parser sync = args.parser;
      SyncStmt(sync);
      return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
    }
    core::Span end_span = TokSpan(args.parser);
    Parser after = args.parser;
    Advance(after);

    // Build MethodCallExpr with parsed arguments
    MethodCallExpr method;
    method.receiver = expr;
    method.name = name.elem;
    method.args = std::move(args.elem);
    return {after, MakeExpr(SpanCover(expr->span, end_span), method)};
  }

  // Propagate: expr?
  if (IsOp(parser, "?")) {
    SPEC_RULE("Postfix-Propagate");
    Parser next = parser;
    core::Span op_span = TokSpan(parser);
    Advance(next);
    PropagateExpr prop;
    prop.value = expr;
    return {next, MakeExpr(SpanCover(expr->span, op_span), prop)};
  }

  // No postfix operator - return expression unchanged
  return {parser, expr};
}

// =============================================================================
// ParsePostfixTail - Apply postfix operators until none remain
// =============================================================================
//
// SPEC: Lines 5133-5142

ParseElemResult<ExprPtr> ParsePostfixTail(Parser parser, ExprPtr expr,
                                           bool allow_brace,
                                           bool allow_bracket) {
  const Token* tok = Tok(parser);
  const bool postfix_start = CallTypeArgsStart(parser) ||
                             (tok && IsPostfixStart(*tok));
  // Stop if no token or not a postfix start
  if (!postfix_start) {
    SPEC_RULE("Parse-PostfixTail-Stop");
    return {parser, expr};
  }
  if (parser.stop_before_parallel_options && IsParallelOptionListStart(parser)) {
    SPEC_RULE("Parse-PostfixTail-Stop");
    return {parser, expr};
  }
  // Skip bracket postfix when allow_bracket is false
  if (!allow_bracket && tok->kind == TokenKind::Punctuator &&
      tok->lexeme == "[") {
    SPEC_RULE("Parse-PostfixTail-Stop");
    return {parser, expr};
  }
  SPEC_RULE("Parse-PostfixTail-Cons");
  ParseElemResult<ExprPtr> step = PostfixStep(parser, expr, allow_bracket);
  return ParsePostfixTail(step.parser, step.elem, allow_brace, allow_bracket);
}

// =============================================================================
// ParseBasePostfix - Parse base postfix expression (primary + suffixes)
// =============================================================================
//
// SPEC: Lines 5095-5098 (Parse-BasePostfix)

ParseElemResult<ExprPtr> ParseBasePostfix(Parser parser, bool allow_brace,
                                          bool allow_bracket) {
  SPEC_RULE("Parse-BasePostfix");
  ParseElemResult<ExprPtr> primary = ParsePrimary(parser, allow_brace);
  return ParsePostfixTail(primary.parser, primary.elem, allow_brace,
                          allow_bracket);
}

// =============================================================================
// ParsePostfix - Main postfix expression entry point
// =============================================================================
//
// SPEC: Lines 5128-5131
// Parses a pipeline expression (base_postfix_expr with optional "=>").

ParseElemResult<ExprPtr> ParsePostfix(Parser parser, bool allow_brace,
                                       bool allow_bracket) {
  SPEC_RULE("Parse-Postfix");
  return ParsePipeline(parser, allow_brace, allow_bracket);
}

}  // namespace cursive::ast
