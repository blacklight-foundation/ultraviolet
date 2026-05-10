// =============================================================================
// MIGRATION MAPPING: let_stmt.cpp
// =============================================================================
// This file should contain parsing logic for let binding statements.
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.10, Lines 6265-6268, 6392-6397
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Binding-Stmt)** Lines 6265-6268
// Tok(P) in {Keyword(`let`), Keyword(`var`)}
// Gamma |- ParseBindingAfterLetVar(P) => (P_1, bind)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_1, LetOrVarStmt(P, bind))
//
// **(LetOrVarStmt-Let)** Lines 6394-6397
// Tok(P) = Keyword(`let`)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- LetOrVarStmt(P, bind) => LetStmt(bind)
//
// **(Parse-BindingAfterLetVar)** Lines 4093-4096
// Tok(P) = kw in {Keyword(`let`), Keyword(`var`)}
// Gamma |- ParsePattern(Advance(P)) => (P_1, pat)
// Gamma |- ParseTypeAnnotOpt(P_1) => (P_2, ty_opt)
// Tok(P_2) in {Operator("="), Operator(":=")}    op = Tok(P_2)
// Gamma |- ParseExpr(Advance(P_2)) => (P_3, init)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseBindingAfterLetVar(P) => (P_3, <pat, ty_opt, op, init, SpanBetween(P, P_3)>)
//
// TERMINATOR RULES (Lines 6360-6370):
// -----------------------------------------------------------------------------
// ReqTerm(s) <=> s in {LetStmt(_), VarStmt(_), ...}
//
// **(ConsumeTerminatorOpt-Req-Yes)** Lines 6362-6365
// ReqTerm(s)    IsTerm(Tok(P))
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ConsumeTerminatorOpt(P, s) => Advance(P)
//
// **(ConsumeTerminatorOpt-Req-No)** Lines 6367-6370
// ReqTerm(s)    NOT IsTerm(Tok(P))
// Emit(Code(Missing-Terminator-Err), Span = Tok(P).span)
// Gamma |- SyncStmt(P) => P_1
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ConsumeTerminatorOpt(P, s) => P_1
//
// SEMANTICS:
// - `let` creates an immutable binding
// - Binding operator `=` creates a movable binding
// - Binding operator `:=` creates an immovable binding
// - Statements require terminator (`;` or newline)
// - Binding consists of: pattern, optional type annotation, operator, initializer
//
// =============================================================================
// SOURCE FILE: cursive-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParseBindingAfterLetVar helper (parser_binding.cpp, called from here)
//    ─────────────────────────────────────────────────────────────────────────
//    - Defined in parser_binding.cpp, but invoked here
//    - Input: Parser at `let` or `var` keyword
//    - Returns: (Parser, Binding)
//    - Called via: ParseBindingAfterLetVar(parser)
//
// 2. LetStmt construction in ParseStmtCore (Lines 382-398)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 382-383: Check for let keyword
//      - if (tok->kind == TokenKind::Keyword &&
//            (tok->lexeme == "let" || tok->lexeme == "var"))
//
//    Lines 384-386: Parse binding
//      - SPEC_RULE("Parse-Binding-Stmt");
//      - const bool is_let = tok->lexeme == "let";
//      - ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);
//
//    Lines 387-392: Construct LetStmt if is_let
//      - if (is_let) {
//          SPEC_RULE("LetOrVarStmt-Let");
//          LetStmt stmt;
//          stmt.binding = std::move(binding.elem);
//          stmt.span = stmt.binding.span;
//          return {binding.parser, stmt, true};
//        }
//
// 3. RequiresTerminator check (Lines 145-158)
//    ─────────────────────────────────────────────────────────────────────────
//    Line 153: LetStmt requires terminator
//      - return std::holds_alternative<LetStmt>(stmt) || ...;
//
// 4. ApplyStmtAttrs for attributes (Lines 174-181)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 178-181: Apply attributes to let statement
//      - if (auto* let_stmt = std::get_if<LetStmt>(&stmt)) {
//          let_stmt->binding.init = WrapAttrExpr(attrs, let_stmt->binding.init);
//          return;
//        }
//
// BINDING DATA STRUCTURE:
// =============================================================================
// struct Binding {
//   Pattern pattern;           // The binding pattern (identifier or destructuring)
//   std::optional<Type> type;  // Optional type annotation
//   std::string op;            // "=" (movable) or ":=" (immovable)
//   ExprPtr init;              // Initializer expression
//   core::Span span;           // Source span
// };
//
// struct LetStmt {
//   Binding binding;
//   core::Span span;
// };
//
// DEPENDENCIES:
// =============================================================================
// - ParseBindingAfterLetVar function (binding.cpp)
// - ParsePattern function (pattern/*.cpp)
// - ParseTypeAnnotOpt function (type_annot.cpp)
// - ParseExpr function (expr/*.cpp)
// - Binding AST node type
// - LetStmt AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - LetStmt and VarStmt share parsing via ParseBindingAfterLetVar
// - The keyword (let vs var) determines which statement type to produce
// - Binding operator (= vs :=) affects movability semantics, not parsing
// - Attributes are applied to the initializer expression
// - Span covers from keyword through initializer
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::IsKwTok;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);

// =============================================================================
// ParseLetStmt - Parse let binding statement
// =============================================================================
//
// SPEC: Lines 6265-6268 (Parse-Binding-Stmt) + 6394-6397 (LetOrVarStmt-Let)
// Assumes parser is at "let" keyword.

ParseElemResult<Stmt> ParseLetStmt(Parser parser) {
  SPEC_RULE("Parse-Binding-Stmt");
  SPEC_RULE("LetOrVarStmt-Let");

  // Parse the binding (pattern, optional type, operator, initializer)
  ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);

  // Construct LetStmt
  LetStmt stmt;
  stmt.binding = std::move(binding.elem);
  stmt.span = stmt.binding.span;

  return {binding.parser, stmt};
}

// =============================================================================
// TryParseLetStmt - Try to parse let statement, returns nullopt if not "let"
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseLetStmt(Parser parser) {
  if (!IsKw(parser, "let")) {
    return std::nullopt;
  }
  return ParseLetStmt(parser);
}

}  // namespace cursive::ast
