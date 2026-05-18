// =============================================================================
// raw_ptr_type.cpp - Raw Pointer Type Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4783-4786
//
// Parses raw pointer types: *imm T and *mut T
// - *imm T: immutable raw pointer (cannot write through it)
// - *mut T: mutable raw pointer (can read and write)
// Usage requires unsafe block.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseRawPtrType - Parse Raw Pointer Type *imm T or *mut T
// =============================================================================
// SPEC: Lines 4783-4786
// Assumes current token is '*'.

ParseElemResult<std::shared_ptr<Type>> ParseRawPtrType(Parser parser) {
  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume '*'

  const Token* qual = Tok(next);
  if (qual && qual->kind == TokenKind::Keyword &&
      (qual->lexeme == "imm" || qual->lexeme == "mut")) {
    SPEC_RULE("Parse-Raw-Pointer-Type");
    const RawPtrQual q =
        qual->lexeme == "imm" ? RawPtrQual::Imm : RawPtrQual::Mut;
    Advance(next);  // consume imm/mut

    ParseElemResult<std::shared_ptr<Type>> elem = ParseType(next);
    TypeRawPtr ptr;
    ptr.qual = q;
    ptr.element = elem.elem;
    return {elem.parser, MakeTypeNode(SpanBetween(start, elem.parser), ptr)};
  }

  EmitParseSyntaxErr(next, TokSpan(next));
  return {next, MakeTypePrim(SpanBetween(start, next), "!")};
}

}  // namespace ultraviolet::ast
