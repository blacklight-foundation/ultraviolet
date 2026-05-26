// =============================================================================
// record_literal.cpp - Record Literal Expression Parsing
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// - Parse-Record-Literal (Lines 5224-5227)
// - Parse-Record-Literal-ModalState (Lines 5219-5222)
// - Parse-FieldInitList-* (Lines 5878-5888)
// - Parse-FieldInit-* (Lines 5891-5899)
// - Parse-FieldInitTail-* (Lines 5975-5988)
//
// =============================================================================

#include "02_source/parser/parser.h"

#include <array>
#include <memory>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"
#include "02_source/parser/type/type_parse_internal.h"

namespace ultraviolet::ast {

// Forward declarations from other modules
ExprPtr MakeExpr(const core::Span& span, ExprNode node);
core::Span SpanCover(const core::Span& start, const core::Span& end);
bool IsPunc(const Parser& parser, std::string_view punc);
bool IsOp(const Parser& parser, std::string_view op);
void SkipNewlines(Parser& parser);
ParseElemResult<ExprPtr> ParseExpr(Parser parser);

// ParseFieldInit - Parse individual field initializer
// =============================================================================
//
// SPEC: Lines 5891-5899
// Two forms:
// - Explicit: name: expr
// - Shorthand: name (becomes name: name)

ParseElemResult<FieldInit> ParseFieldInit(Parser parser) {
  Parser start = parser;
  ParseElemResult<Identifier> name = ParseIdent(parser);
  if (IsPunc(name.parser, ":")) {
    SPEC_RULE("Parse-FieldInit-Explicit");
    Parser after_colon = name.parser;
    Advance(after_colon);
    ParseElemResult<ExprPtr> expr = ParseExpr(after_colon);
    FieldInit init;
    init.name = name.elem;
    init.value = expr.elem;
    init.span = SpanBetween(start, expr.parser);
    return {expr.parser, init};
  }
  SPEC_RULE("Parse-FieldInit-Shorthand");
  IdentifierExpr ident;
  ident.name = name.elem;
  ExprPtr value = MakeExpr(SpanBetween(start, name.parser), ident);
  FieldInit init;
  init.name = name.elem;
  init.value = value;
  init.span = SpanBetween(start, name.parser);
  return {name.parser, init};
}

// =============================================================================
// ParseFieldInitTail - Parse tail of field initializer list
// =============================================================================
//
// SPEC: Lines 5975-5988

ParseElemResult<std::vector<FieldInit>> ParseFieldInitTail(
    Parser parser, std::vector<FieldInit> xs) {
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldInitTail-End");
    return {parser, xs};
  }
  if (IsPunc(parser, ",")) {
    const std::array<EndSetToken, 1> end_set = {EndPunct("}")};
    Parser after = parser;
    Advance(after);
    SkipNewlines(after);
    if (IsPunc(after, "}")) {
      if (TrailingCommaAllowed(parser, end_set)) {
        SPEC_RULE("Parse-FieldInitTail-TrailingComma");
      }
      EmitTrailingCommaErr(parser, end_set);
      after.diags = parser.diags;
      return {after, xs};
    }
    SPEC_RULE("Parse-FieldInitTail-Comma");
    ParseElemResult<FieldInit> field = ParseFieldInit(after);
    xs.push_back(field.elem);
    return ParseFieldInitTail(field.parser, std::move(xs));
  }
  EmitParseSyntaxErr(parser, TokSpan(parser));
  return {parser, xs};
}

// =============================================================================
// ParseFieldInitList - Parse field initializer list
// =============================================================================
//
// SPEC: Lines 5878-5888
// This shared helper accepts an empty list when the next token is `}` so it can
// serve the modal-state brace forms. Ordinary record literals and qualified
// brace applications that require a non-empty payload enforce that restriction
// at their own call sites.

ParseElemResult<std::vector<FieldInit>> ParseFieldInitList(Parser parser) {
  SPEC_RULE("ConstructionListAndShorthandParsingFamily");
  SkipNewlines(parser);
  if (IsPunc(parser, "}")) {
    SPEC_RULE("Parse-FieldInitList-Empty");
    return {parser, {}};
  }
  SPEC_RULE("Parse-FieldInitList-Cons");
  ParseElemResult<FieldInit> first = ParseFieldInit(parser);
  std::vector<FieldInit> fields;
  fields.push_back(first.elem);
  return ParseFieldInitTail(first.parser, std::move(fields));
}

// =============================================================================
// ParseRecordLiteralBody - Parse the body of a record literal after "{"
// =============================================================================
//
// Called after we've consumed the opening brace.
// Returns the fields and expects the closing brace.

ParseElemResult<std::vector<FieldInit>> ParseRecordLiteralBody(Parser parser) {
  ParseElemResult<std::vector<FieldInit>> fields = ParseFieldInitList(parser);
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser sync = fields.parser;
    SyncStmt(sync);
    return {sync, fields.elem};
  }
  return fields;
}

// =============================================================================
// ParseSimpleRecordLiteral - Parse TypeName{ fields }
// =============================================================================
//
// SPEC: Lines 5224-5227
// Assumes parser is positioned at "{" and path has been parsed.

ParseElemResult<ExprPtr> ParseSimpleRecordLiteral(
    Parser parser, Parser start, const TypePath& path) {
  SPEC_RULE("Parse-Record-Literal");
  Parser after_l = parser;
  Advance(after_l);  // consume "{"
  ParseElemResult<std::vector<FieldInit>> fields = ParseFieldInitList(after_l);
  if (fields.elem.empty() && IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser after = fields.parser;
    Advance(after);  // consume "}"
    return {after, MakeExpr(SpanBetween(start, after), ErrorExpr{})};
  }
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser sync = fields.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }
  Parser after = fields.parser;
  Advance(after);  // consume "}"
  RecordExpr rec;
  rec.target = path;
  rec.fields = std::move(fields.elem);
  return {after, MakeExpr(SpanBetween(start, after), rec)};
}

// =============================================================================
// ParseModalStateRecordLiteral - Parse ModalType@State{ fields }
// =============================================================================
//
// SPEC: Lines 5219-5222
// Assumes parser is positioned at "@" and path has been parsed.

ParseElemResult<ExprPtr> ParseModalStateRecordLiteral(
    Parser parser, Parser start, const TypePath& path,
    std::vector<std::shared_ptr<Type>> generic_args) {
  SPEC_RULE("Parse-Record-Literal-ModalState");
  Parser after_at = parser;
  Advance(after_at);  // consume "@"
  ParseElemResult<Identifier> state = ParseIdent(after_at);
  if (!IsPunc(state.parser, "{")) {
    EmitParseSyntaxErr(state.parser, TokSpan(state.parser));
    Parser sync = state.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }
  Parser after_l = state.parser;
  Advance(after_l);  // consume "{"
  ParseElemResult<std::vector<FieldInit>> fields = ParseFieldInitList(after_l);
  if (!IsPunc(fields.parser, "}")) {
    EmitParseSyntaxErr(fields.parser, TokSpan(fields.parser));
    Parser sync = fields.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }
  Parser after = fields.parser;
  Advance(after);  // consume "}"
  RecordExpr rec;
  ModalStateRef modal;
  modal.path = path;
  modal.generic_args = std::move(generic_args);
  SyncModalStateRefFromFields(modal);
  modal.state = state.elem;
  rec.target = modal;
  rec.fields = std::move(fields.elem);
  return {after, MakeExpr(SpanBetween(start, after), rec)};
}

// =============================================================================
// ParseRecordLiteral - Parse record literals (simple or modal state)
// =============================================================================
//
// SPEC: Parse-Record-Literal, Parse-Record-Literal-ModalState
// Entry point used by ParsePrimary when a record literal is detected.

ParseElemResult<ExprPtr> ParseRecordLiteral(Parser parser, bool allow_brace) {
  Parser start = parser;

  ParseElemResult<TypePath> path = ParseTypePath(parser);
  ParseGenericArgsResult gen = ParseGenericArgsOpt(path.parser);
  std::vector<std::shared_ptr<Type>> generic_args =
      gen.args.has_value()
          ? std::move(*gen.args)
          : std::vector<std::shared_ptr<Type>>{};

  // Modal state record literal: ModalType@State{...}
  if (IsOp(gen.parser, "@")) {
    return ParseModalStateRecordLiteral(gen.parser, start, path.elem,
                                        std::move(generic_args));
  }

  if (!allow_brace || !IsPunc(gen.parser, "{")) {
    EmitParseSyntaxErr(gen.parser, TokSpan(gen.parser));
    Parser sync = gen.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  // Simple record literals require an unqualified type path.
  if (path.elem.size() != 1) {
    EmitParseSyntaxErr(gen.parser, TokSpan(gen.parser));
    Parser sync = gen.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  // Generic args are not part of Parse-Record-Literal (non-modal).
  if (gen.args.has_value()) {
    EmitParseSyntaxErr(gen.parser, TokSpan(gen.parser));
    Parser sync = gen.parser;
    SyncStmt(sync);
    return {sync, MakeExpr(SpanBetween(start, sync), ErrorExpr{})};
  }

  return ParseSimpleRecordLiteral(gen.parser, start, path.elem);
}

}  // namespace ultraviolet::ast
