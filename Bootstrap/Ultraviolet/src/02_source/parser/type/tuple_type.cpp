// =============================================================================
// tuple_type.cpp - Tuple Type Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4758-4761
// Section 3.3.7.1 (Tuple Type Elements), Lines 4825-4845
//
// Parses tuple types: (T1, T2, ...) and single-element tuples (T;).
// Note: Unit type () is handled as empty tuple in ParseNonPermType.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseTupleTypeElems - Parse Tuple Type Elements
// =============================================================================
// SPEC: Lines 4827-4845

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTupleTypeElems(
    Parser parser) {
  SkipNewlinesType(parser);

  // Parse-TupleTypeElems-Empty
  if (IsPuncType(parser, ")")) {
    SPEC_RULE("Parse-TupleTypeElems-Empty");
    return {parser, {}};
  }

  // Parse first type
  ParseElemResult<std::shared_ptr<Type>> first = ParseType(parser);
  Parser after_first = first.parser;
  SkipNewlinesType(after_first);

  // Parse-TupleTypeElems-One: Single-element tuple with semicolon
  if (IsPuncType(after_first, ";")) {
    SPEC_RULE("Parse-TupleTypeElems-One");
    Parser after = after_first;
    Advance(after);  // consume ;
    return {after, {first.elem}};
  }

  // Parse-TupleTypeElems-Many or reject the non-canonical singleton comma form.
  if (IsPuncType(after_first, ",")) {
    Parser after = after_first;
    Advance(after);  // consume ,
    SkipNewlinesType(after);

    // `(T,)` is never canonical Ultraviolet syntax. A trailing comma only denotes
    // continuation of a multi-element list, so this is always an error.
    if (IsPuncType(after, ")")) {
      EmitParseSyntaxErr(after_first, TokSpan(after_first));
      return {after, {first.elem}};
    }

    // Parse-TupleTypeElems-Many: Multiple elements
    SPEC_RULE("Parse-TupleTypeElems-Many");
    ParseElemResult<std::shared_ptr<Type>> second = ParseType(after);
    ParseElemResult<std::vector<std::shared_ptr<Type>>> tail =
        ParseTypeListTail(second.parser, {second.elem});

    // Combine first element with tail
    std::vector<std::shared_ptr<Type>> elems;
    elems.reserve(1 + tail.elem.size());
    elems.push_back(first.elem);
    elems.insert(elems.end(), tail.elem.begin(), tail.elem.end());
    return {tail.parser, elems};
  }

  // Error: unexpected token after first element
  EmitParseSyntaxErr(after_first, TokSpan(after_first));
  return {after_first, {first.elem}};
}

}  // namespace ultraviolet::ast
