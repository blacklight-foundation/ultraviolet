// =============================================================================
// union_type.cpp - Union Type Parsing
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.7, Lines 4664-4672
//
// Parses union types: T1 | T2 | T3 representing tagged unions (sum types).
// Union types are unordered: A | B is equivalent to B | A.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace cursive::ast {

// =============================================================================
// ParseUnionTail - Parse Union Type Continuation
// =============================================================================
// SPEC: Lines 4664-4672

ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseUnionTail(
    Parser parser, bool allow_union) {
  // Parse-UnionTail-None: No more union members (or unions disallowed)
  if (!allow_union || !IsOpType(parser, "|")) {
    SPEC_RULE("Parse-UnionTail-None");
    return {parser, {}};
  }

  // Parse-UnionTail-Cons: Another union member
  SPEC_RULE("Parse-UnionTail-Cons");
  Parser next = parser;
  Advance(next);  // consume |

  // Parse next type (without permission, to avoid confusion with union)
  ParseElemResult<std::shared_ptr<Type>> head = ParseNonPermType(next);

  // Recursively parse more union members
  ParseElemResult<std::vector<std::shared_ptr<Type>>> tail =
      ParseUnionTail(head.parser, allow_union);

  // Combine head with tail
  std::vector<std::shared_ptr<Type>> elems;
  elems.reserve(1 + tail.elem.size());
  elems.push_back(head.elem);
  elems.insert(elems.end(), tail.elem.begin(), tail.elem.end());
  return {tail.parser, elems};
}

}  // namespace cursive::ast
