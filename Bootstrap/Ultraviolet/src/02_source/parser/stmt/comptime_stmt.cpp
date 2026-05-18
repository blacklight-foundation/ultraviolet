// =============================================================================
// comptime_stmt.cpp - Compile-Time Statement Parsing
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

ParseElemResult<Stmt> ParseComptimeStmt(Parser parser) {
  SPEC_RULE("Parse-CtStmt");
  Parser start = parser;
  Advance(parser);

  ParseElemResult<std::shared_ptr<Block>> body = ParseBlock(parser);
  parser = body.parser;

  CtStmt stmt;
  stmt.attrs = {};
  stmt.body = body.elem;
  stmt.span = SpanBetween(start, parser);
  return {parser, stmt};
}

}  // namespace ultraviolet::ast
