// =============================================================================
// type_parse_internal.h - Internal Header for Type Parsing
// =============================================================================
//
// This header provides shared utilities and forward declarations for type
// parsing. It is internal to the parser/type module and should not be included
// outside of the type parsing implementation files.
//
// =============================================================================

#ifndef CURSIVE_PARSER_TYPE_INTERNAL_H_
#define CURSIVE_PARSER_TYPE_INTERNAL_H_

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "02_source/lexer/keyword_policy.h"
#include "02_source/parser/parser.h"

namespace cursive::ast {

// Use lexer types
using cursive::lexer::IsIdentTok;
using cursive::lexer::IsKwTok;
using cursive::lexer::IsOpTok;
using cursive::lexer::IsPuncTok;
using cursive::lexer::OpaqueTypeTok;
using cursive::lexer::Token;
using cursive::lexer::TokenKind;

// =============================================================================
// Helper Functions (defined in type_common.cpp)
// =============================================================================

// Skip newline tokens
void SkipNewlinesType(Parser& parser);

// Create a Type node with the given span and variant
std::shared_ptr<Type> MakeTypeNode(const core::Span& span, TypeNode node);

// Create a primitive type node (convenience wrapper)
std::shared_ptr<Type> MakeTypePrim(const core::Span& span,
                                   std::string_view name);

// Token predicate helpers
bool IsOpType(const Parser& parser, std::string_view op);
bool IsPuncType(const Parser& parser, std::string_view punc);
bool IsKwType(const Parser& parser, std::string_view kw);

// =============================================================================
// Primitive Type Predicates (defined in primitive_type.cpp)
// =============================================================================

bool IsIntTypeLexeme(std::string_view lexeme);
bool IsFloatTypeLexeme(std::string_view lexeme);
bool IsPrimLexemeSet(std::string_view lexeme);
bool BuiltinTypePath(const TypePath& path);

// =============================================================================
// Permission Parsing (defined in permission.cpp)
// =============================================================================

struct PermOptResult {
  Parser parser;
  std::optional<TypePerm> perm;
};

PermOptResult ParsePermOpt(Parser parser);

// =============================================================================
// State Parsing (defined in string_type.cpp, bytes_type.cpp, safe_ptr_type.cpp)
// =============================================================================

ParseElemResult<std::optional<StringState>> ParseStringState(Parser parser);
ParseElemResult<std::optional<BytesState>> ParseBytesState(Parser parser);
ParseElemResult<std::optional<PtrState>> ParsePtrState(Parser parser);

// =============================================================================
// Function Type Parsing (defined in function_type.cpp)
// =============================================================================

// Lookahead to check if current position starts a function type
bool HasFuncArrow(const Parser& parser);

// Parse function parameter type
ParseElemResult<TypeFuncParam> ParseParamType(Parser parser);

// Parse parameter type list (inside parentheses)
ParseElemResult<std::vector<TypeFuncParam>> ParseParamTypeList(Parser parser);

// Parse the tail of a parameter type list
ParseElemResult<std::vector<TypeFuncParam>> ParseParamTypeListTail(
    Parser parser, std::vector<TypeFuncParam> ps);

// Parse a complete function type: (T1, T2) -> R
ParseElemResult<std::shared_ptr<Type>> ParseFuncType(Parser parser);

// =============================================================================
// Closure Type Parsing (defined in closure_type.cpp)
// =============================================================================

// Parse closure type: |params| -> R [shared: {deps}]
ParseElemResult<std::shared_ptr<Type>> ParseClosureType(Parser parser);

// =============================================================================
// Tuple Type Parsing (defined in tuple_type.cpp)
// =============================================================================

// Parse tuple type elements inside parentheses
ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTupleTypeElems(
    Parser parser);

// =============================================================================
// Type List Parsing (defined in type_args.cpp)
// =============================================================================

// Parse tail of a type list (after first element)
ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeListTail(
    Parser parser, std::vector<std::shared_ptr<Type>> xs);

// Parse tail of a type list with an explicit terminator set.
ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseTypeListTailWithEndSet(
    Parser parser,
    std::vector<std::shared_ptr<Type>> xs,
    std::span<const EndSetToken> end_set);

// =============================================================================
// Array Type Parsing (defined in array_type.cpp)
// =============================================================================

// Parse array type [T; n] after '[' consumed and element type parsed
ParseElemResult<std::shared_ptr<Type>> ParseArrayType(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> element);

// =============================================================================
// Slice Type Parsing (defined in slice_type.cpp)
// =============================================================================

// Parse slice type [T] after '[' consumed and element type parsed
ParseElemResult<std::shared_ptr<Type>> ParseSliceType(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> element);

// =============================================================================
// Raw Pointer Type Parsing (defined in raw_ptr_type.cpp)
// =============================================================================

// Parse raw pointer type *imm T or *mut T
ParseElemResult<std::shared_ptr<Type>> ParseRawPtrType(Parser parser);

// =============================================================================
// Safe Pointer Type Parsing (defined in safe_ptr_type.cpp)
// =============================================================================

ParseElemResult<std::shared_ptr<Type>> ParseSafePointerType(Parser parser);

// =============================================================================
// Dynamic Type Parsing (defined in dynamic_type.cpp)
// =============================================================================

// Parse dynamic type $ClassName
ParseElemResult<std::shared_ptr<Type>> ParseDynamicType(Parser parser);

// =============================================================================
// Opaque Type Parsing (defined in opaque_type.cpp)
// =============================================================================

// Parse opaque type: opaque Path
ParseElemResult<std::shared_ptr<Type>> ParseOpaqueType(Parser parser);

// =============================================================================
// Type Path Parsing (defined in type_path.cpp)
// =============================================================================

// Result type for generic arguments parsing
struct ParseGenericArgsResult {
  Parser parser;
  std::optional<std::vector<std::shared_ptr<Type>>> args;
};

// Parse required generic type arguments <T, U>
ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseGenericArgs(
    Parser parser);

// Parse optional generic type arguments <T, U>
ParseGenericArgsResult ParseGenericArgsOpt(Parser parser);

// Parse type path with optional generics (returns TypePathType or TypeApply)
ParseElemResult<std::shared_ptr<Type>> ParseTypePathType(
    Parser parser,
    const Parser& start,
    TypePath path);

// =============================================================================
// Modal State Type Parsing (defined in state_specific_type.cpp)
// =============================================================================

// Parse modal state suffix @StateName
ParseElemResult<std::shared_ptr<Type>> ParseModalStateType(
    Parser parser,
    const Parser& start,
    TypePath path,
    std::vector<std::shared_ptr<Type>> generic_args);

// =============================================================================
// Union Type Parsing (defined in union_type.cpp)
// =============================================================================

// Parse continuation of union type: | T2 | T3 ...
// allow_union=false stops at the current position without consuming '|'.
ParseElemResult<std::vector<std::shared_ptr<Type>>> ParseUnionTail(
    Parser parser, bool allow_union = true);

// =============================================================================
// Refinement Clause Parsing (defined in refinement_clause.cpp)
// =============================================================================

// Result type for refinement parsing (same as ParseElemResult<Type>)
using ParseRefinementResult = ParseElemResult<std::shared_ptr<Type>>;

// Parse optional type refinement payload: |: { predicate }
ParseElemResult<ExprPtr> ParseRefinementOpt(Parser parser);

// Parse type refinement: |: { predicate }
ParseRefinementResult ParseRefinementClause(
    Parser parser,
    const Parser& start,
    std::shared_ptr<Type> base);

// =============================================================================
// Non-Permission Type Parsing (defined in type_common.cpp)
// =============================================================================

// Parse a type without leading permission qualifier
// This is the main dispatch point for all type variants
ParseElemResult<std::shared_ptr<Type>> ParseNonPermType(Parser parser);

// Parse a type without union tail (used in contexts where '|' is a delimiter).
ParseElemResult<std::shared_ptr<Type>> ParseTypeNoUnion(Parser parser);

}  // namespace cursive::ast

#endif  // CURSIVE_PARSER_TYPE_INTERNAL_H_
