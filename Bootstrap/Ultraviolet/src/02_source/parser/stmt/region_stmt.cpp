// =============================================================================
// MIGRATION MAPPING: region_stmt.cpp
// =============================================================================
// This file should contain parsing logic for region statements.
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.10, Lines 6305-6328
// =============================================================================
//
// FORMAL RULES FROM SPEC:
// -----------------------------------------------------------------------------
// **(Parse-Region-Opts-None)** Lines 6305-6308
// NOT IsPunc(Tok(P), "(")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseRegionOptsOpt(P) => (P, null)
//
// **(Parse-Region-Opts-Some)** Lines 6310-6313
// IsPunc(Tok(P), "(")
// Gamma |- ParseExpr(Advance(P)) => (P_1, e)
// IsPunc(Tok(P_1), ")")
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseRegionOptsOpt(P) => (Advance(P_1), e)
//
// **(Parse-Region-Alias-None)** Lines 6315-6318
// NOT IsKw(Tok(P), `as`)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseRegionAliasOpt(P) => (P, null)
//
// **(Parse-Region-Alias-Some)** Lines 6320-6323
// IsKw(Tok(P), `as`)
// Gamma |- ParseIdent(Advance(P)) => (P_1, name)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseRegionAliasOpt(P) => (P_1, name)
//
// **(Parse-Region-Stmt)** Lines 6325-6328
// IsKw(Tok(P), `region`)
// Gamma |- ParseRegionOptsOpt(Advance(P)) => (P_1, opts_opt)
// Gamma |- ParseRegionAliasOpt(P_1) => (P_2, alias_opt)
// Gamma |- ParseBlock(P_2) => (P_3, b)
// ────────────────────────────────────────────────────────────────────────────
// Gamma |- ParseStmtCore(P) => (P_3, RegionStmt(opts_opt, alias_opt, b))
//
// SEMANTICS:
// - `region { ... }` creates a new memory arena
// - `region (opts) { ... }` creates region with options (e.g., size)
// - `region as r { ... }` creates named region (for explicit allocation)
// - `region (opts) as r { ... }` combines both options and name
// - Allocations within region use `^` operator
// - Region is deallocated when block exits
//
// =============================================================================
// SOURCE FILE: ultraviolet-bootstrap/src/02_syntax/parser_stmt.cpp
// =============================================================================
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
//
// 1. ParseRegionOptsOpt helper function (Lines 346-361)
//    ─────────────────────────────────────────────────────────────────────────
//    ParseElemResult<ExprPtr> ParseRegionOptsOpt(Parser parser) {
//      if (!IsPunc(parser, "(")) {
//        SPEC_RULE("Parse-Region-Opts-None");
//        return {parser, nullptr};
//      }
//      SPEC_RULE("Parse-Region-Opts-Some");
//      Parser next = parser;
//      Advance(next);  // consume "("
//      ParseElemResult<ExprPtr> expr = ParseExpr(next);
//      if (!IsPunc(expr.parser, ")")) {
//        EmitParseSyntaxErr(expr.parser, TokSpan(expr.parser));
//      } else {
//        Advance(expr.parser);  // consume ")"
//      }
//      return {expr.parser, expr.elem};
//    }
//
// 2. ParseRegionAliasOpt helper function (Lines 363-373)
//    ─────────────────────────────────────────────────────────────────────────
//    ParseElemResult<std::optional<Identifier>> ParseRegionAliasOpt(Parser parser) {
//      if (!IsKw(parser, "as")) {
//        SPEC_RULE("Parse-Region-Alias-None");
//        return {parser, std::nullopt};
//      }
//      SPEC_RULE("Parse-Region-Alias-Some");
//      Parser next = parser;
//      Advance(next);  // consume `as`
//      ParseElemResult<Identifier> name = ParseIdent(next);
//      return {name.parser, name.elem};
//    }
//
// 3. Region statement parsing in ParseStmtCore (Lines 487-501)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 487-488: Check for region keyword
//      - if (IsKw(parser, "region"))
//
//    Lines 489-501: Parse region components
//      - SPEC_RULE("Parse-Region-Stmt");
//        Parser next = parser;
//        Advance(next);  // consume `region`
//        ParseElemResult<ExprPtr> opts = ParseRegionOptsOpt(next);
//        ParseElemResult<std::optional<Identifier>> alias =
//            ParseRegionAliasOpt(opts.parser);
//        ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(alias.parser);
//        RegionStmt stmt;
//        stmt.opts_opt = opts.elem;
//        stmt.alias_opt = alias.elem;
//        stmt.body = block.elem;
//        stmt.span = SpanBetween(start, block.parser);
//        return {block.parser, stmt, true};
//
// 4. ApplyStmtAttrs for attributes (Lines 216-219)
//    ─────────────────────────────────────────────────────────────────────────
//    Lines 216-219: Apply attributes to region statement
//      - if (auto* region = std::get_if<RegionStmt>(&stmt)) {
//          region->opts_opt = WrapAttrExpr(attrs, region->opts_opt);
//          return;
//        }
//
// REGION DATA STRUCTURE:
// =============================================================================
// struct RegionStmt {
//   ExprPtr opts_opt;                      // Optional region options expression
//   std::optional<Identifier> alias_opt;   // Optional region name (for `as r`)
//   std::shared_ptr<Block> body;           // The region body block
//   core::Span span;                       // Source span
// };
//
// REGION OPTIONS (RegionOptions record):
// - size: usize - Hint for initial region size
// - align: usize - Alignment requirement (optional)
//
// DEPENDENCIES:
// =============================================================================
// - IsKw helper function (parser utilities)
// - IsPunc helper function (parser utilities)
// - ParseExpr function (expr/*.cpp)
// - ParseIdent function (identifier.cpp)
// - ParseBlock function (block.cpp or stmt_common.cpp)
// - RegionStmt AST node type
// - Block AST node type
// - ConsumeTerminatorOpt function (stmt_common.cpp)
// - WrapAttrExpr function (stmt_common.cpp)
//
// =============================================================================
// REFACTORING NOTES:
// =============================================================================
// - Region has optional opts: `region` vs `region (opts)`
// - Region has optional alias: `region { }` vs `region as r { }`
// - Order is: region (opts)? (as name)? block
// - ParseRegionOptsOpt returns nullptr if no parentheses
// - ParseRegionAliasOpt returns std::nullopt if no `as` keyword
// - Named regions allow explicit allocation via `r ^ value`
// - Span covers from `region` keyword to closing brace
// - Attributes are applied to the options expression if present
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
bool IsKw(const Parser& parser, std::string_view kw);
bool IsPunc(const Parser& parser, std::string_view p);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);
ParseLocalIdentResult ParseLocalIdent(Parser parser);
ParseElemResult<std::shared_ptr<Block>> ParseBlock(Parser parser);

struct ParseRegionAliasOptResult {
  Parser parser;
  std::optional<Identifier> alias_opt;
  std::optional<SpliceIdentNode> alias_splice_opt;
};

// =============================================================================
// ParseRegionOptsOpt - Parse optional region options
// =============================================================================
//
// SPEC: Lines 6305-6313

ParseElemResult<ExprPtr> ParseRegionOptsOpt(Parser parser) {
  if (!IsPunc(parser, "(")) {
    SPEC_RULE("Parse-Region-Opts-None");
    return {parser, nullptr};
  }
  SPEC_RULE("Parse-Region-Opts-Some");
  Parser next = parser;
  Advance(next);  // consume "("

  ParseElemResult<ExprPtr> expr = ParseExpr(next);
  Parser after = expr.parser;
  if (!IsPunc(after, ")")) {
    EmitParseSyntaxErr(after, TokSpan(after));
  } else {
    Advance(after);  // consume ")"
  }
  return {after, expr.elem};
}

// =============================================================================
// ParseRegionAliasOpt - Parse optional region alias (as name)
// =============================================================================
//
// SPEC: Lines 6315-6323

ParseRegionAliasOptResult ParseRegionAliasOpt(Parser parser) {
  if (!IsKw(parser, "as")) {
    SPEC_RULE("Parse-Region-Alias-None");
    return {parser, std::nullopt, std::nullopt};
  }
  SPEC_RULE("Parse-Region-Alias-Some");
  Parser next = parser;
  Advance(next);  // consume "as"

  ParseLocalIdentResult name = ParseLocalIdent(next);
  return {name.parser, std::move(name.name), std::move(name.splice_opt)};
}

// =============================================================================
// ParseRegionStmt - Parse region statement
// =============================================================================
//
// SPEC: Lines 6325-6328 (Parse-Region-Stmt)
// `region (opts)? (as name)? { ... }` creates memory arena.

ParseElemResult<Stmt> ParseRegionStmt(Parser parser) {
  SPEC_RULE("Parse-Region-Stmt");
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume "region"

  ParseElemResult<ExprPtr> opts = ParseRegionOptsOpt(next);
  ParseRegionAliasOptResult alias = ParseRegionAliasOpt(opts.parser);
  ParseElemResult<std::shared_ptr<Block>> block = ParseBlock(alias.parser);

  RegionStmt stmt;
  stmt.opts_opt = opts.elem;
  stmt.alias_opt = std::move(alias.alias_opt);
  stmt.alias_splice_opt = std::move(alias.alias_splice_opt);
  stmt.body = block.elem;
  stmt.span = SpanBetween(start, block.parser);

  return {block.parser, stmt};
}

// =============================================================================
// TryParseRegionStmt - Try to parse region statement
// =============================================================================

std::optional<ParseElemResult<Stmt>> TryParseRegionStmt(Parser parser) {
  if (!IsKw(parser, "region")) {
    return std::nullopt;
  }
  return ParseRegionStmt(parser);
}

}  // namespace ultraviolet::ast
