// =============================================================================
// MIGRATION MAPPING: method_call.cpp
// =============================================================================
// This file contains parsing logic for method call expressions: expr~>name(args).
//
// SPEC REFERENCE: CursiveSpecification.md, Lines 5438-5441
// -----------------------------------------------------------------------------
// **(Postfix-MethodCall)** Lines 5438-5441
// IsOp(Tok(P), "~>")    Γ ⊢ ParseIdent(Advance(P)) ⇓ (P_1, name)    IsPunc(Tok(P_1), "(")
// Γ ⊢ ParseArgList(Advance(P_1)) ⇓ (P_2, args)    IsPunc(Tok(P_2), ")")
// ────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
// Γ ⊢ PostfixStep(P, e) ⇓ (Advance(P_2), MethodCall(e, name, args))
//
// SEMANTICS:
// - Method calls use the ~> operator: receiver~>methodName(arg1, arg2, ...)
// - The receiver expression is evaluated first
// - Then the method name is resolved
// - Then arguments are evaluated left-to-right
// - Arguments are comma-separated, optionally prefixed with "move"
// - IMPORTANT: Cursive uses ~> for method calls, NOT dot (.)
//   Dot is reserved for field access and tuple indexing
//
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_expr.cpp
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// 1. Method Call Branch (in PostfixStep function)
//    Source: parser_expr.cpp, lines 806-834
//    ```cpp
//    if (IsOp(parser, "~>")) {
//      SPEC_RULE("Postfix-MethodCall");
//      Parser next = parser;
//      Advance(next);
//      ParseElemResult<Identifier> name = ParseIdent(next);
//      if (!IsPunc(name.parser, "(")) {
//        EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
//        Parser sync = name.parser;
//        SyncStmt(sync);
//        return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
//      }
//      Parser after_l = name.parser;
//      Advance(after_l);
//      ParseElemResult<std::vector<Arg>> args = ParseArgList(after_l);
//      if (!IsPunc(args.parser, ")")) {
//        EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
//        Parser sync = args.parser;
//        SyncStmt(sync);
//        return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
//      }
//      core::Span end_span = TokSpan(args.parser);
//      Parser after = args.parser;
//      Advance(after);
//      MethodCallExpr call;
//      call.receiver = expr;
//      call.name = name.elem;
//      call.args = std::move(args.elem);
//      return {after, MakeExpr(SpanCover(expr->span, end_span), call)};
//    }
//    ```
//
// ERROR HANDLING:
// - Missing "(" after method name: syntax error, sync to statement
// - Missing ")" after arguments: syntax error, sync to statement
// - Error recovery uses SyncStmt to skip to statement boundary
//
// AST DEFINITIONS (from ast.h, lines 490-494):
// ```cpp
// struct MethodCallExpr {
//   ExprPtr receiver;
//   Identifier name;
//   std::vector<Arg> args;
// };
// ```
//
// DEPENDENCIES:
// - Requires: ParseIdent (for method name parsing)
// - Requires: ParseArgList (for argument list parsing) - see call.cpp
// - Requires: MethodCallExpr AST node type
// - Requires: MakeExpr, SpanCover, SpanBetween helpers
// - Requires: IsOp, IsPunc, Tok, TokSpan, Advance helpers
// - Requires: EmitParseSyntaxErr, SyncStmt for error handling
// - Requires: Arg struct for argument representation
//
// REFACTORING NOTES:
// - The ~> operator is handled as IsOp, not IsPunc (it's an operator token)
// - Method name MUST be immediately followed by "(" - no generic args on methods
//   (generic type arguments remain part of ordinary postfix call parsing)
// - Span covers from receiver expression start to closing ")"
// - ParseArgList is shared with regular function calls (see call.cpp)
// - The receiver expression can be any valid postfix expression (chaining)
//   Example: obj~>foo()~>bar()~>baz()
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Forward declarations from expr_common.cpp and other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view punc);

// Forward declaration from call.cpp
ParseElemResult<std::vector<Arg>> ParseArgList(Parser parser);

// =============================================================================
// ParseMethodCall - Parse method call expression: receiver~>name(args)
// =============================================================================
//
// SPEC: Lines 5438-5441 (Postfix-MethodCall)
//
// Called when the parser is positioned at a "~>" operator. Parses the method
// name and argument list.
//
// Parameters:
//   parser   - Parser positioned at the "~>" operator
//   receiver - The receiver expression (left of ~>)
//
// Returns:
//   ParseElemResult with MethodCallExpr if successful
//   ParseElemResult with ErrorExpr on errors (missing "(", missing ")")
//
// IMPORTANT: Cursive uses ~> for method calls, NOT dot (.)
// Dot is reserved for field access and tuple indexing.

ParseElemResult<ExprPtr> ParseMethodCall(Parser parser, ExprPtr receiver) {
  SPEC_RULE("Postfix-MethodCall");
  Parser next = parser;
  Advance(next);  // consume "~>"

  // Parse the method name
  ParseElemResult<Identifier> name = ParseIdent(next);

  // Method name MUST be immediately followed by "("
  if (!IsPunc(name.parser, "(")) {
    EmitParseSyntaxErr(name.parser, TokSpan(name.parser));
    Parser sync = name.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  Parser after_l = name.parser;
  Advance(after_l);  // consume "("

  // Parse argument list
  ParseElemResult<std::vector<Arg>> args = ParseArgList(after_l);

  // Expect closing ")"
  if (!IsPunc(args.parser, ")")) {
    EmitParseSyntaxErr(args.parser, TokSpan(args.parser));
    Parser sync = args.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(parser, sync), ErrorExpr{})};
  }

  core::Span end_span = TokSpan(args.parser);
  Parser after = args.parser;
  Advance(after);  // consume ")"

  MethodCallExpr call;
  call.receiver = receiver;
  call.name = name.elem;
  call.args = std::move(args.elem);
  return {after, MakeExpr(SpanCover(receiver->span, end_span), call)};
}

}  // namespace cursive::ast
