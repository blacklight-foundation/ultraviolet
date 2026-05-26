// =============================================================================
// MIGRATION MAPPING: var_stmt.cpp
// =============================================================================
// This file should contain parsing logic for var binding statements.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.10, Lines 6265-6268, 6399-6402
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
// **(LetOrVarStmt-Var)** Lines 6399-6402
// Tok(P) = Keyword(`var`)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- LetOrVarStmt(P, bind) => VarStmt(bind)
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
// - `var` creates a mutable binding
// - Binding operator `=` creates a movable binding
// - Binding operator `:=` creates an immovable binding
// - Statements require terminator (`;` or newline)
// - Binding consists of: pattern, optional type annotation, operator, initializer
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. VarStmt construction in ParseStmtCore (Lines 382-398)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 382-383: Check for var keyword (shared with let)
//      - if (tok->kind == TokenKind::Keyword &&
//            (tok->lexeme == "let" || tok->lexeme == "var"))
//
//    Lines 384-386: Parse binding
//      - SPEC_RULE("Parse-Binding-Stmt");
//      - const bool is_let = tok->lexeme == "let";
//      - ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);
//
//    Lines 393-398: Construct VarStmt if not is_let
//      - SPEC_RULE("LetOrVarStmt-Var");
//        VarStmt stmt;
//        stmt.binding = std::move(binding.elem);
//        stmt.span = stmt.binding.span;
//        return {binding.parser, stmt, true};
//
// 2. RequiresTerminator check (Lines 145-158)
//    ─────────────────────────────────────────────────────────────────────────
//    Line 154: VarStmt requires terminator
//      - return ... || std::holds_alternative<VarStmt>(stmt) || ...;
//
// 3. ApplyStmtAttrs for attributes (Lines 182-186)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 182-186: Apply attributes to var statement
//      - if (auto* var_stmt = std::get_if<VarStmt>(&stmt)) {
//          var_stmt->binding.init = WrapAttrExpr(attrs, var_stmt->binding.init);
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
// struct VarStmt {
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
// - VarStmt AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - LetStmt and VarStmt share parsing via ParseBindingAfterLetVar
// - The keyword (let vs var) determines which statement type to produce
// - VarStmt differs from LetStmt only in mutability semantics
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

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::IsKwTok;
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);

// =============================================================================
// ParseVarStmt - Parse var binding statement
// =============================================================================
//
// SPEC: Lines 6265-6268 (Parse-Binding-Stmt) + 6399-6402 (LetOrVarStmt-Var)
// Assumes parser is at "var" keyword.

ParseElemResult<Stmt> ParseVarStmt(Parser parser) {
  SPEC_RULE("Parse-Binding-Stmt");
  SPEC_RULE("LetOrVarStmt-Var");

  // Parse the binding (pattern, optional type, operator, initializer)
  ParseElemResult<Binding> binding = ParseBindingAfterLetVar(parser);

  // Construct VarStmt
  VarStmt stmt;
  stmt.binding = std::move(binding.elem);
  stmt.span = stmt.binding.span;

  return {binding.parser, stmt};
}

// =============================================================================
// TryParseVarStmt - Try to parse var statement, returns nullopt if not "var"
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseVarStmt(Parser parser) {
  if (!IsKw(parser, "var")) {
    return std::nullopt;
  }
  return ParseVarStmt(parser);
}

}  // namespace ultraviolet::ast
