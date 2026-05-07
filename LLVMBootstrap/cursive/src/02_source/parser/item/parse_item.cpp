// =============================================================================
// parse_item.cpp - Top-Level Item Parsing Dispatcher
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 3.3.6.1 (Item Parsing Rules)
//
// This file implements the main item parsing dispatcher:
//   - ParseItem: Dispatch to specific item parsers based on leading keywords
//
// The dispatcher handles:
//   1. Optional attributes ([[...]])
//   2. Special cases before visibility (import, modal class, extern, use, return)
//   3. Visibility modifier (public, internal, private)
//   4. Keyword dispatch (using, let/var, procedure, record, enum, modal, class, type)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "02_source/lexer/keyword_policy.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::Token;
using cursive::lexer::TokenKind;
using cursive::lexer::IsIdentTok;

// Forward declarations for helper functions (from item_common.cpp)
void SkipNewlines(Parser& parser);
bool IsWhereTok(const Parser& parser);
bool IsKw(const Parser& parser, std::string_view kw);
void EmitReturnAtModuleErr(Parser& parser);

// Forward declarations for parsing functions
ParseElemResult<AttrOpt> ParseAttributeListOpt(Parser parser);

// Forward declarations for individual item parsers
ParseItemResult ParseImportDecl(Parser parser, Visibility vis, AttrOpt attrs_opt);
ParseItemResult ParseUsingDecl(Parser item_start,
                               Parser parser,
                               Visibility vis,
                               AttrOpt attrs_opt);
ParseItemResult ParseStaticDecl(Parser parser, Visibility vis, AttrOpt attrs_opt);
ParseItemResult ParseProcedureDecl(Parser parser,
                                   Visibility vis,
                                   AttributeList attrs,
                                   bool visibility_explicit);
ParseItemResult ParseComptimeProcedureDecl(Parser parser, AttributeList attrs);
ParseItemResult ParseRecordDecl(Parser parser, Visibility vis, AttributeList attrs);
ParseItemResult ParseEnumDecl(Parser parser, Visibility vis, AttributeList attrs);
ParseItemResult ParseModalDecl(Parser parser, Visibility vis, AttributeList attrs);
ParseItemResult ParseClassDecl(Parser parser, Visibility vis, AttributeList attrs,
                               bool is_modal = false);
ParseItemResult ParseTypeAliasDecl(Parser parser, Visibility vis, AttributeList attrs);
ParseItemResult ParseExternBlock(Parser item_start, Parser parser, Visibility vis,
                                 AttrOpt attrs_opt);
ParseItemResult ParseDeriveTargetDecl(Parser parser);

namespace {

bool IsLexemeToken(const Parser& parser, std::string_view lexeme) {
  const Token* tok = Tok(parser);
  return tok && ((tok->kind == TokenKind::Identifier) ||
                 (tok->kind == TokenKind::Keyword)) &&
         tok->lexeme == lexeme;
}

}  // namespace

// =============================================================================
// ParseItem - Parse a single top-level item
// =============================================================================
//
// SPEC: Parse-Item-Dispatch
//   Γ ⊢ ParseAttrListOpt(P) ⇓ (P_0, attrs_opt)
//   SkipNewlines(P_0) ⇓ P_1
//   Dispatch based on Tok(P_1):
//     - `import` -> Parse-Import
//     - `modal` `class` -> Parse-Modal-Class
//     - `extern` -> Parse-Extern-Block
//     - `use` -> Unsupported
//     - `return` -> Error
//     - else -> ParseVis(P_1) then dispatch on keyword
//   ────────────────────────────────────────────────────────────────────
//   Γ ⊢ ParseItem(P) ⇓ (P_n, item)

ParseItemResult ParseItem(Parser parser) {
  // Item boundaries may be separated by one or more newline terminators.
  // Normalize those before looking for an attribute list so attributes can
  // legally appear on the line before the declaration they modify.
  SkipNewlines(parser);

  Parser start = parser;

  if (AtEof(parser)) {
    return {parser, ErrorItem{core::Span{}, {}}};
  }

  // Parse optional attributes
  ParseElemResult<AttrOpt> attrs = ParseAttributeListOpt(parser);
  AttrOpt attrs_opt = attrs.elem;
  AttributeList attrs_list = attrs_opt.value_or(AttributeList{});
  parser = attrs.parser;
  while (true) {
    Parser next_attrs = parser;
    SkipNewlines(next_attrs);
    ParseElemResult<AttrOpt> more_attrs = ParseAttributeListOpt(next_attrs);
    if (!more_attrs.elem.has_value()) {
      parser = next_attrs;
      break;
    }
    attrs_list.insert(attrs_list.end(), more_attrs.elem->begin(),
                      more_attrs.elem->end());
    attrs_opt = attrs_list;
    parser = more_attrs.parser;
  }

  SkipNewlines(parser);

  // Handle stray where clause at top level (error case)
  if (IsWhereTok(parser)) {
    SPEC_RULE("Parse-Stray-Where");
    EmitParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }

  // Handle import keyword (before visibility)
  if (IsKw(parser, "import")) {
    SPEC_RULE("Parse-Import");
    // Import declarations without explicit visibility use Parse-Vis-Default.
    return ParseImportDecl(parser, Visibility::Internal, attrs_opt);
  }

  // Handle "modal class" (modal class declaration before visibility check)
  if (IsKw(parser, "modal")) {
    ParseElemResult<bool> modal_opt = ParseModalOpt(parser);
    const Token* next_tok = Tok(modal_opt.parser);
    if (modal_opt.elem &&
        (IsKw(modal_opt.parser, "class") ||
         (next_tok && IsIdentTok(*next_tok) && next_tok->lexeme == "class"))) {
      // This is "modal class" - a modal class declaration
      SPEC_RULE("Parse-Modal-Class");
      // ParseClassDecl expects parser to be at "class" keyword
      return ParseClassDecl(modal_opt.parser, Visibility::Internal, attrs_list,
                            true);
    }
    // Not "modal class" - fall through to handle "modal" type declaration below
  }

  // Handle "use" (not valid as a top-level item in Cursive grammar)
  if (const Token* tok = Tok(parser);
      tok && IsIdentTok(*tok) && tok->lexeme == "use") {
    SPEC_RULE("Parse-Item-Err");
    EmitGenericParseSyntaxErr(parser, TokSpan(parser));
    Parser next = parser;
    Advance(next);
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }

  // Handle return at module level (error case)
  if (IsKw(parser, "return")) {
    SPEC_RULE("Return-At-Module-Err");
    EmitReturnAtModuleErr(parser);
    Parser next = parser;
    Advance(next);
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }

  // Handle "comptime procedure" before visibility parsing.
  if (IsKw(parser, "comptime")) {
    Parser after_comptime = parser;
    Advance(after_comptime);
    ParseElemResult<Visibility> ct_vis = ParseVis(after_comptime);
    if (IsKw(ct_vis.parser, "procedure")) {
      SPEC_RULE("Parse-CtProc");
      return ParseComptimeProcedureDecl(parser, attrs_list);
    }
  }

  // =========================================================================
  // Rust-confusion detection (before visibility parsing)
  // Detect common Rust keywords that are not valid Cursive and emit targeted
  // diagnostics to help users (and LLMs) correct their code.
  // =========================================================================
  if (const Token* tok = Tok(parser); tok) {
    const auto& lex = tok->lexeme;
    // "pub" -> Cursive uses "public"
    if (IsIdentTok(*tok) && lex == "pub") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(parser));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `pub` with `public`",
                                  TokSpan(parser), "public"});
        core::Emit(parser.diags, *diag);
      }
      // Advance past "pub" and try to continue parsing the rest
      Parser next = parser;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    // "fn" -> Cursive uses "procedure"
    if (IsIdentTok(*tok) && lex == "fn") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(parser));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `fn` with `procedure`",
                                  TokSpan(parser), "procedure"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = parser;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    // "struct" -> Cursive uses "record"
    if (IsIdentTok(*tok) && lex == "struct") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(parser));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `struct` with `record`",
                                  TokSpan(parser), "record"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = parser;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    // "impl" -> Cursive has no impl blocks
    if (IsIdentTok(*tok) && lex == "impl") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(parser));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::Help,
                                  "define methods inside the `record` body instead",
                                  {}, {}});
        core::Emit(parser.diags, *diag);
      }
      Parser next = parser;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    // "trait" -> Cursive uses "class"
    if (IsIdentTok(*tok) && lex == "trait") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(parser));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `trait` with `class`",
                                  TokSpan(parser), "class"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = parser;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
  }

  // Parse visibility modifier
  ParseElemResult<Visibility> vis = ParseVis(parser);
  const bool visibility_explicit = vis.parser.index != parser.index;
  Parser cur = vis.parser;

  if (IsLexemeToken(cur, "derive")) {
    if (!attrs_list.empty() || cur.index != parser.index) {
      EmitParseSyntaxErr(parser, TokSpan(parser));
      Parser next = cur;
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    SPEC_RULE("Parse-Derive-Target");
    return ParseDeriveTargetDecl(cur);
  }

  // Dispatch based on keyword after visibility

  // import declaration (visibility applies)
  if (IsKw(cur, "import")) {
    SPEC_RULE("Parse-Import");
    return ParseImportDecl(cur, vis.elem, attrs_opt);
  }

  // extern block (visibility applies)
  if (IsKw(cur, "extern")) {
    SPEC_RULE("Parse-Extern-Block");
    return ParseExternBlock(start, cur, vis.elem, attrs_opt);
  }

  if (const Token* tok = Tok(cur); tok && IsIdentTok(*tok) &&
                                  tok->lexeme == "extern") {
    EmitParseSyntaxErr(cur, TokSpan(cur));
    Parser next = cur;
    Advance(next);
    SyncItem(next);
    return {next, ErrorItem{SpanBetween(start, next), {}}};
  }

  // using declaration
  if (IsKw(cur, "using")) {
    return ParseUsingDecl(start, cur, vis.elem, attrs_opt);
  }

  // static declaration (let/var)
  if (IsKw(cur, "let") || IsKw(cur, "var")) {
    SPEC_RULE("Parse-Static-Decl");
    return ParseStaticDecl(cur, vis.elem, attrs_opt);
  }

  if (IsKw(cur, "procedure")) {
    SPEC_RULE("Parse-Procedure");
    return ParseProcedureDecl(cur, vis.elem, attrs_list, visibility_explicit);
  }

  // record declaration
  if (IsKw(cur, "record")) {
    SPEC_RULE("Parse-Record");
    return ParseRecordDecl(cur, vis.elem, attrs_list);
  }

  // enum declaration
  if (IsKw(cur, "enum")) {
    SPEC_RULE("Parse-Enum");
    return ParseEnumDecl(cur, vis.elem, attrs_list);
  }

  // modal type declaration, modal class declaration, or class declaration
  if (IsKw(cur, "modal") || IsKw(cur, "class")) {
    ParseElemResult<bool> modal_opt = ParseModalOpt(cur);
    Parser class_parser = modal_opt.parser;
    const Token* class_tok = Tok(class_parser);
    const bool is_class_decl =
        IsKw(class_parser, "class") ||
        (class_tok && IsIdentTok(*class_tok) && class_tok->lexeme == "class");

    if (modal_opt.elem && is_class_decl) {
      SPEC_RULE("Parse-Modal-Class");
      return ParseClassDecl(class_parser, vis.elem, attrs_list, true);
    }
    if (modal_opt.elem) {
      SPEC_RULE("Parse-Modal");
      return ParseModalDecl(cur, vis.elem, attrs_list);
    }
    if (is_class_decl) {
      SPEC_RULE("Parse-Class");
      return ParseClassDecl(class_parser, vis.elem, attrs_list, false);
    }
  }

  // type alias declaration
  if (IsKw(cur, "type")) {
    SPEC_RULE("Parse-Type-Alias");
    return ParseTypeAliasDecl(cur, vis.elem, attrs_list);
  }

  // Rust-confusion detection (after visibility modifier)
  // Catches cases like "public fn foo()" or "internal struct Bar"
  if (const Token* tok = Tok(cur); tok && IsIdentTok(*tok)) {
    const auto& lex = tok->lexeme;
    if (lex == "fn") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(cur));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `fn` with `procedure`",
                                  TokSpan(cur), "procedure"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = cur;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    if (lex == "struct") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(cur));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `struct` with `record`",
                                  TokSpan(cur), "record"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = cur;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    if (lex == "impl") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(cur));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::Help,
                                  "define methods inside the `record` body instead",
                                  {}, {}});
        core::Emit(parser.diags, *diag);
      }
      Parser next = cur;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
    if (lex == "trait") {
      auto diag = core::MakeDiagnosticById("E-SRC-0520", TokSpan(cur));
      if (diag) {
        diag->children.push_back({core::SubDiagnosticKind::FixIt,
                                  "replace `trait` with `class`",
                                  TokSpan(cur), "class"});
        core::Emit(parser.diags, *diag);
      }
      Parser next = cur;
      Advance(next);
      SyncItem(next);
      return {next, ErrorItem{SpanBetween(start, next), {}}};
    }
  }

  // Unknown item - emit error and recover
  SPEC_RULE("Parse-Item-Err");
  EmitGenericParseSyntaxErr(parser, TokSpan(parser));
  Parser next = AdvanceOrEOF(parser);
  SyncItem(next);
  return {next, ErrorItem{SpanBetween(start, next), {}}};
}

}  // namespace cursive::ast
