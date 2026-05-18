// =============================================================================
// MIGRATION MAPPING: call.cpp
// =============================================================================
// This file contains parsing logic for function call expressions: expr(args).
// Method calls and calls with type arguments are in separate files.
//
// SPEC REFERENCE: SPECIFICATION.md, Lines 5428-5431, 5448-5473, 5943-5958
// -----------------------------------------------------------------------------
// **(Postfix-Call)** Lines 5428-5431
// IsPunc(Tok(P), "(")    Γ ⊢ ParseArgList(Advance(P)) ⇓ (P_1, args)    IsPunc(Tok(P_1), ")")
// ────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ PostfixStep(P, e) ⇓ (Advance(P_1), Call(e, args))
//
// ARGUMENT LIST RULES (Lines 5448-5473):
// **(Parse-ArgList-Empty)** Lines 5450-5453
// IsPunc(Tok(P), ")")
// ──────────────────────────────────────────────
// Γ ⊢ ParseArgList(P) ⇓ (P, [])
//
// **(Parse-ArgList-Cons)** Lines 5455-5458
// Γ ⊢ ParseArg(P) ⇓ (P_1, a)    Γ ⊢ ParseArgTail(P_1, [a]) ⇓ (P_2, args)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseArgList(P) ⇓ (P_2, args)
//
// **(Parse-Arg)** Lines 5460-5463
// Γ ⊢ ParseArgMoveOpt(P) ⇓ (P_1, moved)    Γ ⊢ ParseExpr(P_1) ⇓ (P_2, e)
// ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseArg(P) ⇓ (P_2, ⟨moved, e, SpanBetween(P, P_2)⟩)
//
// **(Parse-ArgMoveOpt-None)** Lines 5465-5468
// ¬ IsKw(Tok(P), `move`)
// ──────────────────────────────────────────────────────────────
// Γ ⊢ ParseArgMoveOpt(P) ⇓ (P, false)
//
// **(Parse-ArgMoveOpt-Yes)** Lines 5470-5473
// IsKw(Tok(P), `move`)
// ──────────────────────────────────────────────────────
// Γ ⊢ ParseArgMoveOpt(P) ⇓ (Advance(P), true)
//
// ARG TAIL RULES (Lines 5943-5958):
// **(Parse-ArgTail-End)** Lines 5945-5948
// IsPunc(Tok(P), ")")
// ───────────────────────────────────────────
// Γ ⊢ ParseArgTail(P, xs) ⇓ (P, xs)
//
// **(Parse-ArgTail-TrailingComma)** Lines 5950-5953
// IsPunc(Tok(P), ",")    IsPunc(Tok(Advance(P)), ")")    TrailingCommaAllowed(P_0, P, {Punctuator(")")})
// ────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseArgTail(P, xs) ⇓ (Advance(P), xs)
//
// **(Parse-ArgTail-Comma)** Lines 5955-5958
// IsPunc(Tok(P), ",")    Γ ⊢ ParseArg(Advance(P)) ⇓ (P_1, a)    Γ ⊢ ParseArgTail(P_1, xs ++ [a]) ⇓ (P_2, ys)
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ ParseArgTail(P, xs) ⇓ (P_2, ys)
//
// SEMANTICS:
// - Function calls: expr(arg1, arg2, ...)
// - Callee expression is evaluated first, then arguments left-to-right
// - Arguments are comma-separated, optionally prefixed with "move"
// - Trailing commas allowed ONLY when closing ")" is on different line
// - Empty argument lists are valid: expr()
//
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_expr.cpp
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// 1. Postfix Call Branch (in PostfixStep function)
//    Source: parser_expr.cpp, lines 787-805
//    ```cpp
//    if (IsPunc(parser, "(")) {
//      SPEC_RULE("Postfix-Call");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<std::vector<Arg>> args = ParseArgList(next);
//      if (!IsPunc(args.parser, ")")) {
//        EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
//        Parser sync = args.parser;
//        SyncStmt(sync);
//        return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
//      }
//      core::Span end_span = TokSpan(args.parser);
//      Parser after = args.parser;
//      Advance(after);
//      CallExpr call;
//      call.callee = expr;
//      call.args = std::move(args.elem);
//      return {after, MakeExpr(SpanCover(expr->span, end_span), call)};
//    }
//    ```
//
// 2. ParseArgList function
//    Source: parser_expr.cpp, lines 1926-1937
//    ```cpp
//    ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser) {
//      SkipNewlines(parser);
//      if (IsPunc(parser, ")")) {
//        SPEC_RULE("Parse-ArgList-Empty");
//        return {parser, {}};
//      }
//      SPEC_RULE("Parse-ArgList-Cons");
//      ParseElemResult<Arg> first = ParseArg(parser);
//      std::vector<Arg> args;
//      args.push_back(first.elem);
//      return ParseArgTail(first.parser, std::move(args));
//    }
//    ```
//
// 3. ParseArg function
//    Source: parser_expr.cpp, lines 1939-1949
//    ```cpp
//    ParseElemResult<Arg> ParseArg(Parser parser) {
//      SPEC_RULE("Parse-Arg");
//      Parser start = parser;
//      ParseElemResult<bool> moved = ParseArgMoveOpt(parser);
//      ParseElemResult<ExprPtr> expr = ParseExpr(moved.parser);
//      Arg arg;
//      arg.moved = moved.elem;
//      arg.value = expr.elem;
//      arg.span = SpanBetween(start, expr.parser);
//      return {expr.parser, arg};
//    }
//    ```
//
// 4. ParseArgMoveOpt function
//    Source: parser_expr.cpp, lines 1951-1960
//    ```cpp
//    ParseElemResult<bool> ParseArgMoveOpt(Parser parser) {
//      if (!IsKw(parser, "move")) {
//        SPEC_RULE("Parse-ArgMoveOpt-None");
//        return {parser, false};
//      }
//      SPEC_RULE("Parse-ArgMoveOpt-Yes");
//      Parser next = parser;
//      Advance(next);
//      return {next, true};
//    }
//    ```
//
// 5. ParseArgTail function
//    Source: parser_expr.cpp, lines 1962-1987
//    ```cpp
//    ParseElemResult<std::vector<Arg>> ParseArgTail(Parser parser,
//                                                   std::vector<Arg> xs) {
//      SkipNewlines(parser);
//      if (IsPunc(parser, ")")) {
//        SPEC_RULE("Parse-ArgTail-End");
//        return {parser, xs};
//      }
//      if (IsPunc(parser, ",")) {
//        const std::array<TokenKindMatch, 1> end_set = {MatchPunct(")")};
//        Parser after = parser;
//        Advance(after);
//        SkipNewlines(after);
//        if (IsPunc(after, ")")) {
//          SPEC_RULE("Parse-ArgTail-TrailingComma");
//          EmitTrailingCommaErr(parser, end_set);
//          after.diags = parser.diags;
//          return {after, xs};
//        }
//        SPEC_RULE("Parse-ArgTail-Comma");
//        ParseElemResult<Arg> arg = ParseArg(after);
//        xs.push_back(arg.elem);
//        return ParseArgTail(arg.parser, std::move(xs));
//      }
//      EmitParseSyntaxErr(parser, TokSpan(parser));
//      return {parser, xs};
//    }
//    ```
//
// AST DEFINITIONS (from ast.h):
// ```cpp
// struct Arg {           // lines 194-198
//   bool moved = false;
//   ExprPtr value;
//   core::Span span;
// };
//
// struct CallExpr {      // lines 484-488
//   ExprPtr callee;
//   std::vector<std::shared_ptr<Type>> generic_args;  // empty for simple calls
//   std::vector<Arg> args;
// };
// ```
//
// DEPENDENCIES:
// - Requires: ParseExpr (for argument expression parsing)
// - Requires: Arg, CallExpr AST node types
// - Requires: MakeExpr, SpanCover, SpanBetween helpers
// - Requires: IsPunc, IsKw, Tok, TokSpan, Advance helpers
// - Requires: SkipNewlines (for multi-line arg lists)
// - Requires: EmitParseSyntaxErr, EmitTrailingCommaErr, SyncStmt
// - Requires: TokenKindMatch, MatchPunct for trailing comma detection
//
// REFACTORING NOTES:
// - SkipNewlines is called at start of ParseArgList AND ParseArgTail
// - Trailing comma handling: EmitTrailingCommaErr is called but parsing continues
// - Note: after.diags = parser.diags propagates diagnostic after trailing comma
// - Error recovery uses SyncStmt to skip to statement boundary on missing ")"
// - Span for each Arg covers from move keyword (if present) to expression end
// - Span for full CallExpr covers from callee start to closing ")"
// - The generic_args field on CallExpr is empty for simple calls (no type args)
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsKw(const Parser& parser, std::string_view kw);
bool IsPunc(const Parser& parser, std::string_view punc);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// =============================================================================
// ParseArgMoveOpt - Parse optional "move" keyword before argument
// =============================================================================
//
// SPEC: Lines 5465-5473
// Returns true if "move" was present, false otherwise.

ParseElemResult<bool> ParseArgMoveOpt(Parser parser) {
  if (!IsKw(parser, "move")) {
    SPEC_RULE("Parse-ArgMoveOpt-None");
    return {parser, false};
  }
  SPEC_RULE("Parse-ArgMoveOpt-Yes");
  Parser next = parser;
  Advance(next);
  return {next, true};
}

// =============================================================================
// ParseArg - Parse a single argument (optionally with move)
// =============================================================================
//
// SPEC: Lines 5460-5463

ParseElemResult<Arg> ParseArg(Parser parser) {
  SPEC_RULE("Parse-Arg");
  Parser start = parser;
  ParseElemResult<bool> moved = ParseArgMoveOpt(parser);
  ParseElemResult<ExprPtr> expr = ParseExpr(moved.parser);
  Arg arg;
  arg.moved = moved.elem;
  arg.value = expr.elem;
  arg.span = SpanBetween(start, expr.parser);
  return {expr.parser, arg};
}

// =============================================================================
// ParseArgTail - Parse rest of argument list after first argument
// =============================================================================
//
// SPEC: Lines 5943-5958

ParseElemResult<std::vector<Arg>> ParseArgTail(ListState<Arg> state) {
  const std::array<EndSetToken, 1> end_set = {EndPunct(")")};

  for (;;) {
    SkipNewlines(state.parser);
    if (ListDone(state, end_set)) {
      SPEC_RULE("Parse-ArgTail-End");
      return {state.parser, std::move(state.elems)};
    }

    if (!IsPunc(state.parser, ",")) {
      EmitParseSyntaxErr(state.parser, TokSpan(state.parser));
      return {state.parser, std::move(state.elems)};
    }

    Parser after = state.parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, ")")) {
      if (TrailingCommaAllowed(state.parser, end_set)) {
        SPEC_RULE("Parse-ArgTail-TrailingComma");
      }
      EmitTrailingCommaErr(state.parser, end_set);
      after.diags = state.parser.diags;
      return {after, std::move(state.elems)};
    }

    SPEC_RULE("Parse-ArgTail-Comma");
    state.parser = after;
    state = ListCons(std::move(state), ParseArg);
  }
}

// =============================================================================
// ParseArgList - Parse argument list (between parentheses)
// =============================================================================
//
// SPEC: Lines 5448-5458
// Used by call expressions and method calls.

ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser) {
  SPEC_RULE("ArgumentListParsingFamily");
  SkipNewlines(parser);
  const std::array<EndSetToken, 1> end_set = {EndPunct(")")};
  ListState<Arg> state = ListStart<Arg>(parser);
  if (ListDone(state, end_set)) {
    SPEC_RULE("Parse-ArgList-Empty");
    return {state.parser, {}};
  }
  SPEC_RULE("Parse-ArgList-Cons");
  state = ListCons(std::move(state), ParseArg);
  return ParseArgTail(std::move(state));
}

// =============================================================================
// ParsePostfixCall - Parse function call postfix: (args)
// =============================================================================
//
// SPEC: Lines 5428-5431 (Postfix-Call)
// Called from PostfixStep when "(" is seen.

ParseElemResult<ExprPtr> ParsePostfixCall(Parser parser, ExprPtr callee) {
  SPEC_RULE("Postfix-Call");
  Parser next = parser;
  Advance(next);  // consume "("
  ParseElemResult<std::vector<Arg>> args = ParseArgList(next);
  if (!IsPunc(args.parser, ")")) {
    EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
    Parser sync = args.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }
  core::Span end_span = TokSpan(args.parser);
  Parser after = args.parser;
  Advance(after);  // consume ")"
  CallExpr call;
  call.callee = callee;
  call.args = std::move(args.elem);
  return {after, MakeExpr(SpanCover(callee->span, end_span), call)};
}

}  // namespace ultraviolet::ast
