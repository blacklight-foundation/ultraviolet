// =============================================================================
// using_local_stmt.cpp - Local Using Statement Parsing
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §18.3.2 Parsing
//     (Parse-UsingLocal-Stmt) — parses `using <ident> as <ident>` at statement
//     position into a UsingLocalStmt AST node.
//
// =============================================================================

#include "02_source/parser/parser.h"

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::ast {

bool IsKw(const Parser& parser, std::string_view kw);
ParseElemResult<Identifier> ParseIdent(Parser parser);

// SPEC: (Parse-UsingLocal-Stmt) — §18.3.2
ParseElemResult<Stmt> ParseUsingLocalStmt(Parser parser) {
  Parser start = parser;

  // Consume `using` keyword.
  if (!IsKw(parser, "using")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    return {parser, ErrorStmt{TokSpan(parser)}};
  }
  Parser next = parser;
  Advance(next);

  // Parse source identifier.
  ParseElemResult<Identifier> source = ParseIdent(next);

  // Expect `as`.
  if (!IsKw(source.parser, "as")) {
    EmitParseSyntaxErr(source.parser, TokSpan(source.parser));
    return {source.parser,
            ErrorStmt{SpanBetween(start, source.parser)}};
  }
  Parser after_as = source.parser;
  Advance(after_as);

  // Parse alias identifier.
  ParseElemResult<Identifier> alias = ParseIdent(after_as);

  SPEC_RULE("Parse-UsingLocal-Stmt");
  UsingLocalStmt stmt;
  stmt.source = std::move(source.elem);
  stmt.alias = std::move(alias.elem);
  stmt.span = SpanBetween(start, alias.parser);
  return {alias.parser, std::move(stmt)};
}

}  // namespace ultraviolet::ast
