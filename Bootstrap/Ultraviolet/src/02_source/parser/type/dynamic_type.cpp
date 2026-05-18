// =============================================================================
// dynamic_type.cpp - Dynamic/Capability Type Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md, Section 3.3.7, Lines 4798-4801
//
// Parses dynamic types: $ClassName
// - Used for capability types: $IO, $HeapAllocator, $ExecutionDomain
// - The $ prefix indicates dynamic dispatch
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// ParseDynamicType - Parse Dynamic Type $ClassName
// =============================================================================
// SPEC: Lines 4798-4801
// Assumes current token is '$'.

ParseElemResult<std::shared_ptr<Type>> ParseDynamicType(Parser parser) {
  SPEC_RULE("Parse-Dynamic-Type");

  Parser start = parser;
  Parser next = parser;
  Advance(next);  // consume '$'

  ParseElemResult<TypePath> path = ParseTypePath(next);
  TypeDynamic dyn;
  dyn.path = std::move(path.elem);
  return {path.parser, MakeTypeNode(SpanBetween(start, path.parser), dyn)};
}

}  // namespace ultraviolet::ast
