// =============================================================================
// parse_stmt.cpp - Main Statement Parsing Dispatcher
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.10, Lines 6255-6431
// - Parse-Statement (Lines 6257-6260)
// - ParseStmtCore dispatcher (Lines 6262-6348)
//
// This file implements:
// - ParseStmt: Main entry point for statement parsing
// - ParseStmtCore: Dispatcher for all statement types
//
// Statement types dispatched:
// - let/var bindings
// - using <identifier> as <identifier> (local alias)
// - return, break, continue
// - defer, unsafe, region, frame
// - key blocks (#path { })
// - assignment statements
// - expression statements
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::IsIdentTok;
using ultraviolet::lexer::IsOpTok;
using ultraviolet::lexer::IsPuncTok;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations from parser utilities
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);

// Forward declarations from other modules
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseElemResult<ExprPtr> ParseExprOpt(Parser parser);
ParseElemResult<ExprPtr> ParsePlace(Parser parser, bool allow_brace = true);
ParseElemResult<Binding> ParseBindingAfterLetVar(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

// Forward declarations from statement modules
bool IsExprStartToken(const Token& tok);
bool EndsWithBlock(const ExprPtr& expr);
bool RequiresTerminator(const Stmt& stmt);
void ApplyStmtAttrs(const AttributeList& attrs, Stmt& stmt);
ExprPtr WrapAttrExpr(const AttributeList& attrs, const ExprPtr& expr);
bool IsPlace(const ExprPtr& expr);

// Forward declarations for individual statement parsers
ParseElemResult<Stmt> ParseUsingLocalStmt(Parser parser);
std::optional<ParseElemResult<Stmt>> TryParseKeyBlockStmt(Parser parser);
ParseElemResult<Stmt> ParseComptimeStmt(Parser parser);
ParseElemResult<Identifier> ParseIdent(Parser parser);

// =============================================================================
// IsAssignOp - Check if token is an assignment operator
// =============================================================================

static bool IsAssignOp(const Token& tok) {
  if (tok.kind != TokenKind::Operator) return false;
  return tok.lexeme == "=" || tok.lexeme == "+=" || tok.lexeme == "-=" ||
         tok.lexeme == "*=" || tok.lexeme == "/=" || tok.lexeme == "%=";
}

static bool IsCompoundAssignOp(const Token& tok) {
  if (tok.kind != TokenKind::Operator) return false;
  return tok.lexeme == "+=" || tok.lexeme == "-=" || tok.lexeme == "*=" ||
         tok.lexeme == "/=" || tok.lexeme == "%=";
}

// =============================================================================
// ParseStmtCoreResult - Internal result type for ParseStmtCore
// =============================================================================

struct ParseStmtCoreResult {
  Parser parser;
  Stmt stmt;
  bool matched = false;  // true if a statement was parsed
};

// =============================================================================
// ParseRegionOptsOpt - Parse optional region options
// =============================================================================

static ParseElemResult<ExprPtr> ParseRegionOptsOpt(Parser parser) {
  if (!IsPunc(parser, "(")) {
    return {parser, nullptr};
  }
  Parser next = parser;
  Advance(next);
  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  if (!IsPunc(expr.parser, ")")) {
    EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
  } else {
    Advance(expr.parser);
  }
  return {expr.parser, expr.elem};
}

// =============================================================================
// ParseRegionAliasOpt - Parse optional region alias (as name)
// =============================================================================

static ParseElemResult<std::optional<Identifier>> ParseRegionAliasOpt(
    Parser parser) {
  if (!IsKw(parser, "as")) {
    return {parser, std::nullopt};
  }
  Parser next = parser;
  Advance(next);
  ParseElemResult<Identifier> name = ParseIdent(next);
  return {name.parser, name.elem};
}

// =============================================================================
// ParseStmtCore - Parse statement without terminator handling
// =============================================================================

static ParseStmtCoreResult ParseStmtCore(Parser parser) {
  Parser start = parser;
  const Token* tok = Tok(parser);
  if (!tok) {
    return {parser, ErrorStmt{TokSpan(parser)}, false};
  }

  // let or var binding
  if (tok->kind == TokenKind::Keyword &&
      (tok->lexeme == "let" || tok->lexeme == "var")) {
    SPEC_RULE("Parse-Binding-Stmt");
    const bool is_let = tok->lexeme == "let";
    ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);
    if (is_let) {
      SPEC_RULE("LetOrVarStmt-Let");
      LetStmt stmt;
      stmt.binding = std::move(binding.elem);
      stmt.span = stmt.binding.span;
      return {binding.parser, stmt, true};
    }
    SPEC_RULE("LetOrVarStmt-Var");
    VarStmt stmt;
    stmt.binding = std::move(binding.elem);
    stmt.span = stmt.binding.span;
    return {binding.parser, stmt, true};
  }

  // using <identifier> as <identifier> (local alias)
  if (IsKw(parser, "using")) {
    Parser after_using = parser;
    Advance(after_using);
    const Token* next_tok = Tok(after_using);
    if (next_tok && next_tok->kind == TokenKind::Identifier) {
      auto result = ParseUsingLocalStmt(parser);
      return {result.parser, std::move(result.elem), true};
    }
  }

  // return statement
  if (IsKw(parser, "return")) {
    SPEC_RULE("Parse-Return-Stmt");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> expr = ParseExprOpt(next);
    ReturnStmt stmt;
    stmt.value_opt = expr.elem;
    stmt.span = SpanBetween(start, expr.parser);
    return {expr.parser, stmt, true};
  }

  // break statement
  if (IsKw(parser, "break")) {
    SPEC_RULE("Parse-Break-Stmt");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> expr = ParseExprOpt(next);
    BreakStmt stmt;
    stmt.value_opt = expr.elem;
    stmt.span = SpanBetween(start, expr.parser);
    return {expr.parser, stmt, true};
  }

  // continue statement
  if (IsKw(parser, "continue")) {
    SPEC_RULE("Parse-Continue-Stmt");
    Parser next = parser;
    Advance(next);
    ContinueStmt stmt;
    stmt.span = SpanBetween(start, next);
    return {next, stmt, true};
  }

  // unsafe block statement
  if (IsKw(parser, "unsafe")) {
    SPEC_RULE("Parse-Unsafe-Block");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
    UnsafeBlockStmt stmt;
    stmt.body = block.elem;
    stmt.span = SpanBetween(start, block.parser);
    return {block.parser, stmt, true};
  }

  // defer statement
  if (IsKw(parser, "defer")) {
    SPEC_RULE("Parse-Defer-Stmt");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
    DeferStmt stmt;
    stmt.body = block.elem;
    stmt.span = SpanBetween(start, block.parser);
    return {block.parser, stmt, true};
  }

  // region statement
  if (IsKw(parser, "region")) {
    SPEC_RULE("Parse-Region-Stmt");
    Parser next = parser;
    Advance(next);
    ParseElemResult<ExprPtr> opts = ParseRegionOptsOpt(next);
    ParseElemResult<std::optional<Identifier>> alias =
        ParseRegionAliasOpt(opts.parser);
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(alias.parser);
    RegionStmt stmt;
    stmt.opts_opt = opts.elem;
    stmt.alias_opt = alias.elem;
    stmt.body = block.elem;
    stmt.span = SpanBetween(start, block.parser);
    return {block.parser, stmt, true};
  }

  // frame statement
  if (IsKw(parser, "frame")) {
    SPEC_RULE("Parse-Frame-Stmt");
    Parser next = parser;
    Advance(next);
    ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(next);
    FrameStmt stmt;
    stmt.target_opt = std::nullopt;
    stmt.body = block.elem;
    stmt.span = SpanBetween(start, block.parser);
    return {block.parser, stmt, true};
  }

  // comptime statement
  if (IsKw(parser, "comptime")) {
    Parser after_comptime = parser;
    Advance(after_comptime);
    if (IsPunc(after_comptime, "{")) {
      ParseElemResult<Stmt> stmt = ParseComptimeStmt(parser);
      return {stmt.parser, std::move(stmt.elem), true};
    }
  }

  // key block: #path { ... }
  if (IsOp(parser, "#")) {
    auto key_stmt = TryParseKeyBlockStmt(parser);
    if (key_stmt.has_value()) {
      return {key_stmt->parser, std::move(key_stmt->elem), true};
    }
  }

  // Explicit frame with target: name.frame { ... }
  if (tok->kind == TokenKind::Identifier) {
    Parser after_name = parser;
    Advance(after_name);
    if (IsPunc(after_name, ".")) {
      Parser after_dot = after_name;
      Advance(after_dot);
      if (IsKw(after_dot, "frame")) {
        SPEC_RULE("Parse-Frame-Explicit");
        Identifier name = tok->lexeme;
        Parser after_frame = after_dot;
        Advance(after_frame);
        ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(after_frame);
        FrameStmt stmt;
        stmt.target_opt = name;
        stmt.body = block.elem;
        stmt.span = SpanBetween(start, block.parser);
        return {block.parser, stmt, true};
      }
    }
  }

  // Check for expression start - needed for expr stmt or assignment
  if (!IsExprStartToken(*tok)) {
    return {parser, ErrorStmt{TokSpan(parser)}, false};
  }

  // Assignment lookahead uses ParsePlace as required by Parse-Assign-Stmt.
  Parser place_probe = Clone(parser);
  ParseElemResult<ExprPtr> place = ParsePlace(place_probe, true);
  const Token* op = Tok(place.parser);
  if (op && IsAssignOp(*op)) {
    SPEC_RULE("Parse-Assign-Stmt");
    if (!IsPlace(place.elem)) {
      EmitParseSyntaxErr(place.parser, TokSpan(parser));
      Parser sync = place.parser;
      SyncStmt(sync);
      return {sync, ErrorStmt{SpanBetween(start, sync)}, true};
    }
    Parser next = place.parser;
    Advance(next);
    ParseElemResult<ExprPtr> rhs = ParseExpr(next);
    if (IsCompoundAssignOp(*op)) {
      SPEC_RULE("AssignOrCompound-Compound");
      CompoundAssignStmt stmt;
      stmt.place = place.elem;
      stmt.op = op->lexeme;
      stmt.value = rhs.elem;
      stmt.span = SpanBetween(start, rhs.parser);
      return {rhs.parser, stmt, true};
    }
    SPEC_RULE("AssignOrCompound-Assign");
    AssignStmt stmt;
    stmt.place = place.elem;
    stmt.value = rhs.elem;
    stmt.span = SpanBetween(start, rhs.parser);
    return {rhs.parser, stmt, true};
  }

  // Parse expression (fallback: expression statement)
  ParseElemResult<ExprPtr> expr = ParseExpr(parser);
  if (expr.elem && std::holds_alternative<ErrorExpr>(expr.elem->node)) {
    return {expr.parser, ErrorStmt{SpanBetween(start, expr.parser)}, true};
  }

  // Expression statement (fallback)
  SPEC_RULE("Parse-Expr-Stmt");
  ExprStmt stmt;
  stmt.value = expr.elem;
  stmt.span = SpanBetween(start, expr.parser);
  return {expr.parser, stmt, true};
}

// =============================================================================
// ParseStmt - Main statement parsing entry point
// =============================================================================
//
// SPEC: Lines 6257-6260 (Parse-Statement)
// Parses a statement with optional attributes and terminator handling.

ParseElemResult<Stmt> ParseStmt(Parser parser) {
  // Parse optional attributes
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  parser = attrs.parser;

  if (attrs.elem.has_value()) {
    while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
      Advance(parser);
    }
  }

  // Parse statement core
  ParseStmtCoreResult core = ParseStmtCore(parser);
  if (!core.matched) {
    SPEC_RULE_AT("Parse-Statement-Err", TokSpan(parser));
    EmitGenericParseSyntaxErr(parser, TokSpan(parser));
    Parser next = AdvanceOrEOF(parser);
    Parser sync = next;
    SyncStmt(sync);
    return {sync, ErrorStmt{SpanBetween(parser, sync)}};
  }

  // Apply attributes
  if (attrs.elem.has_value()) {
    ApplyStmtAttrs(*attrs.elem, core.stmt);
  }

  // Handle terminator
  SPEC_RULE_AT("Parse-Statement", TokSpan(parser));
  Parser next = core.parser;
  TerminatorPolicy policy = RequiresTerminator(core.stmt)
                                ? TerminatorPolicy::Required
                                : TerminatorPolicy::Optional;
  ConsumeTerminatorOpt(next, policy);
  return {next, std::move(core.stmt)};
}

}  // namespace ultraviolet::ast
