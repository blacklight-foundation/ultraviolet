// =============================================================================
// extern_block.cpp - Extern Block Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.6.3 (Extern Block Rules)
//
// This file implements extern block parsing for FFI declarations:
//   - ParseExternAbiOpt: Parse optional ABI specifier
//   - ParseExternProcDecl: Parse extern procedure declaration
//   - ParseExternBlock: Parse complete extern block
//
// SYNTAX:
//   extern "C" { procedure malloc(size: usize) -> *mut u8 }
//   extern C { procedure puts(s: *imm i8) -> i32 }
//
// ABI STRINGS: "C", "C-unwind", "system", "stdcall", "fastcall", "vectorcall"
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <memory>
#include <optional>
#include <vector>

#include "00_core/assert_spec.h"
#include "02_source/lexer/keyword_policy.h"

namespace ultraviolet::ast {

// Use lexer types
using ultraviolet::lexer::Token;
using ultraviolet::lexer::TokenKind;
using ultraviolet::lexer::IsIdentTok;

// Forward declarations for helper functions
bool IsKw(const Parser& parser, std::string_view kw);
bool IsOp(const Parser& parser, std::string_view op);
bool IsPunc(const Parser& parser, std::string_view p);
void ConsumeTerminatorReq(Parser& parser);

// Forward declarations for parsing functions
struct SignatureResult {
  Parser parser;
  std::vector<Param> params;
  TypePtr return_type_opt;
};

SignatureResult ParseSignature(Parser parser);
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);
ParseElemResult<std::optional<GenericParams>> ParseGenericParamsOpt(
    Parser parser);
ParseElemResult<std::optional<PredicateClause>> ParsePredicateClauseOpt(
    Parser parser);
ParseElemResult<std::optional<ContractClause>> ParseContractClauseOpt(
    Parser parser);
ParseElemResult<std::optional<std::vector<ForeignContractClause>>>
ParseForeignContractClauseListOpt(Parser parser);

struct ExternItemListResult {
  Parser parser;
  std::vector<ExternItem> items;
};

ExternItemListResult ParseExternItemList(Parser parser);

bool StartsWherePredicateClause(Parser parser) {
  if (!IsOp(parser, "|:")) {
    return false;
  }
  Parser probe = parser;
  Advance(probe);  // consume |:
  while (Tok(probe) && Tok(probe)->kind == TokenKind::Newline) {
    Advance(probe);
  }
  const Token* pred_tok = Tok(probe);
  if (!pred_tok || pred_tok->kind != TokenKind::Identifier) {
    return false;
  }
  Parser after_pred = probe;
  Advance(after_pred);
  while (Tok(after_pred) && Tok(after_pred)->kind == TokenKind::Newline) {
    Advance(after_pred);
  }
  return IsPunc(after_pred, "(");
}

// =============================================================================
// ParseExternAbiOpt - Parse optional ABI specifier
// =============================================================================
//
// ABI can be a string literal ("C") or an identifier (C)

struct ExternAbiOptResult {
  Parser parser;
  std::optional<ExternAbi> abi_opt;
};

ExternAbiOptResult ParseExternAbiOpt(Parser parser) {
  const Token* tok = Tok(parser);
  if (!tok) {
    SPEC_RULE("Parse-ExternAbiOpt-None");
    return {parser, std::nullopt};
  }

  if (tok->kind == TokenKind::StringLiteral) {
    SPEC_RULE("Parse-ExternAbiOpt-String");
    ExternAbiString abi_str;
    abi_str.literal = *tok;
    Advance(parser);
    return {parser, abi_str};
  }

  if (IsIdentTok(*tok)) {
    SPEC_RULE("Parse-ExternAbiOpt-Ident");
    ExternAbiIdent abi_ident;
    abi_ident.name = Identifier{tok->lexeme};
    Advance(parser);
    return {parser, abi_ident};
  }

  SPEC_RULE("Parse-ExternAbiOpt-None");
  return {parser, std::nullopt};
}

// =============================================================================
// ParseExternProcDecl - Parse extern procedure declaration
// =============================================================================

ParseElemResult<ExternProcDecl> ParseExternProcDecl(Parser parser) {
  SPEC_RULE("Parse-ExternProcDecl");
  Parser start = parser;

  // Parse optional attributes
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  parser = attrs.parser;
  AttributeList attrs_list = attrs.elem.value_or(AttributeList{});

  // Skip newlines after attributes
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  // Parse visibility
  ParseElemResult<Visibility> vis = ParseVis(parser);
  parser = vis.parser;

  // Expect 'procedure' keyword
  if (!IsKw(parser, "procedure")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
  } else {
    Advance(parser);
  }

  // Parse procedure name
  ParseElemResult<Identifier> name = ParseIdent(parser);
  parser = name.parser;

  // Parse optional generic parameters
  ParseElemResult<std::optional<GenericParams>> gen_params =
      ParseGenericParamsOpt(parser);
  parser = gen_params.parser;

  // Parse signature
  SignatureResult sig = ParseSignature(parser);
  parser = sig.parser;

  // Parse optional generic constraint clause.
  std::optional<PredicateClause> where_clause_opt = std::nullopt;
  if (StartsWherePredicateClause(parser)) {
    ParseElemResult<std::optional<PredicateClause>> where_clause =
        ParsePredicateClauseOpt(parser);
    parser = where_clause.parser;
    where_clause_opt = where_clause.elem;
  }

  // Parse optional contract clause
  ParseElemResult<std::optional<ContractClause>> contract =
      ParseContractClauseOpt(parser);
  parser = contract.parser;

  // Parse optional foreign contract clauses
  ParseElemResult<std::optional<std::vector<ForeignContractClause>>>
      foreign_contracts = ParseForeignContractClauseListOpt(parser);
  parser = foreign_contracts.parser;

  ExternProcDecl proc;
  proc.attrs = attrs_list;
  proc.vis = vis.elem;
  proc.name = name.elem;
  proc.generic_params = gen_params.elem;
  proc.where_clause = where_clause_opt;
  proc.params = sig.params;
  proc.return_type_opt = sig.return_type_opt;
  proc.contract = contract.elem;
  proc.foreign_contracts_opt = std::move(foreign_contracts.elem);
  ConsumeTerminatorReq(parser);
  proc.span = SpanBetween(start, parser);

  return {parser, proc};
}

ExternItemListResult ParseExternItemList(Parser parser) {
  while (Tok(parser) && Tok(parser)->kind == TokenKind::Newline) {
    Advance(parser);
  }

  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-ExternItemList-End");
    return {parser, {}};
  }

  if (AtEof(parser)) {
    return {parser, {}};
  }

  SPEC_RULE("Parse-ExternItemList-Cons");

  ParseElemResult<ExternProcDecl> proc = ParseExternProcDecl(parser);
  parser = proc.parser;

  ExternItemListResult rest = ParseExternItemList(parser);
  std::vector<ExternItem> items;
  items.reserve(rest.items.size() + 1);
  items.push_back(proc.elem);
  for (ExternItem& item : rest.items) {
    items.push_back(std::move(item));
  }
  return {rest.parser, std::move(items)};
}

// =============================================================================
// ParseExternBlock - Parse complete extern block
// =============================================================================
//
// SPEC: Parse-ExternBlock
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   Γ ⊢ ParseVis(P_0) ⇓ (P_1, vis)
//   IsKw(Tok(P_1), `extern`)
//   Γ ⊢ ParseExternAbiOpt(Advance(P_1)) ⇓ (P_2, abi_opt)
//   IsPunc(Tok(P_2), "{")
//   Γ ⊢ ParseExternItemList(Advance(P_2)) ⇓ (P_3, items)
//   IsPunc(Tok(P_3), "}")
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (Advance(P_3), ⟨ExternBlock, ...⟩)
//
// Note: callers enter this parser at the `extern` keyword after attribute and
// visibility parsing, matching §11.4.2 Parse-ExternBlock.

ParseItemResult ParseExternBlock(Parser item_start, Parser parser,
                                 Visibility vis, AttrOpt attrs_opt) {
  SPEC_RULE("Parse-ExternBlock");
  Parser start = item_start;

  if (!IsKw(parser, "extern")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    SyncItem(parser);
    return {parser, ErrorItem{SpanBetween(start, parser), {}}};
  }

  Advance(parser);  // consume "extern"

  // Parse optional ABI
  ExternAbiOptResult abi = ParseExternAbiOpt(parser);
  parser = abi.parser;

  // Expect opening brace
  if (!IsPunc(parser, "{")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
    SyncItem(parser);
    return {parser, ErrorItem{SpanBetween(start, parser), {}}};
  }
  Advance(parser);

  ExternItemListResult items = ParseExternItemList(parser);
  parser = items.parser;

  // Expect closing brace
  if (!IsPunc(parser, "}")) {
    EmitParseSyntaxErr(parser, TokSpan(parser));
  } else {
    Advance(parser);
  }

  ExternBlock block;
  block.attrs_opt = std::move(attrs_opt);
  block.vis = vis;
  block.abi_opt = abi.abi_opt;
  block.items = std::move(items.items);
  block.span = SpanBetween(start, parser);

  return {parser, block};
}

}  // namespace ultraviolet::ast
