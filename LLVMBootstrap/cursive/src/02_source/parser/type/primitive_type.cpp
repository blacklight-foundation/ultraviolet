// =============================================================================
// primitive_type.cpp - Primitive Type Parsing and Predicates
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md, Section 3.3.7, Lines 4695-4711
//
// Parses primitive types (i32, u64, bool, char), unit type "()", and never
// type "!". Also defines predicates for identifying primitive type lexemes.
//
// PrimLexemeSet = {i8, i16, i32, i64, i128, u8, u16, u32, u64, u128,
//                  isize, usize, f16, f32, f64, bool, char}
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace cursive::ast {

// =============================================================================
// Primitive Type Lexeme Predicates
// =============================================================================

bool IsIntTypeLexeme(std::string_view lexeme) {
  return lexeme == "i8" || lexeme == "i16" || lexeme == "i32" ||
         lexeme == "i64" || lexeme == "i128" || lexeme == "u8" ||
         lexeme == "u16" || lexeme == "u32" || lexeme == "u64" ||
         lexeme == "u128" || lexeme == "isize" || lexeme == "usize";
}

bool IsFloatTypeLexeme(std::string_view lexeme) {
  return lexeme == "f16" || lexeme == "f32" || lexeme == "f64";
}

bool IsPrimLexemeSet(std::string_view lexeme) {
  return IsIntTypeLexeme(lexeme) || IsFloatTypeLexeme(lexeme) ||
         lexeme == "bool" || lexeme == "char";
}

bool BuiltinTypePath(const TypePath& path) {
  if (path.size() != 1) {
    return false;
  }
  const std::string_view name = path[0];
  if (IsPrimLexemeSet(name)) {
    return true;
  }
  return name == "string" || name == "bytes";
}

// =============================================================================
// Primitive Type Parsing
// =============================================================================
// Note: Actual parsing of primitives, unit, and never types is handled
// inline in ParseNonPermType (type_common.cpp) since they are part of
// the main dispatch logic. This file exports the predicates needed there.

}  // namespace cursive::ast
