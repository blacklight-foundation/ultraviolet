// ===========================================================================
// ast_patterns.h - Pattern AST Node Definitions
// ===========================================================================
//
// Pattern nodes for case analysis, let bindings, and loop iteration.
// Also includes IfCaseClause for if-is case analysis support.
//
// SPEC: docs/CursiveSpecification.md Section 3.3.2.5
//
// ===========================================================================

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Core dependencies
#include "02_source/ast/ast_common.h"

namespace cursive::ast {

// Core aliases, helper types, and enums are defined in ast_common.h and ast_enums.h.

// ---------------------------------------------------------------------------
// 1. BASIC PATTERNS
// ---------------------------------------------------------------------------

/// Literal pattern - matches literal values (42, "hello", true)
struct LiteralPattern {
    Token literal;
};

/// Wildcard pattern - matches anything (_)
struct WildcardPattern {};

/// Identifier pattern - simple binding (x)
struct IdentifierPattern {
    Identifier name;
    std::optional<SpliceIdentNode> name_splice_opt;
};

/// Typed pattern - binding with type annotation (x: T)
struct TypedPattern {
    Identifier name;
    std::shared_ptr<Type> type;
    std::optional<SpliceIdentNode> name_splice_opt;
};

// ---------------------------------------------------------------------------
// 2. COMPOUND PATTERNS
// ---------------------------------------------------------------------------

/// Tuple pattern - destructures tuples ((a, b))
struct TuplePattern {
    std::vector<PatternPtr> elements;
};

/// Field pattern - record field with optional nested pattern
/// Supports both Point{x, y} (shorthand) and Point{x: a, y: b} (explicit)
struct FieldPattern {
    Identifier name;
    PatternPtr pattern_opt;  // nullptr for shorthand like Point{x}
    Span span;
};

/// Record pattern - destructures records (Point{x, y})
struct RecordPattern {
    TypePath path;
    std::vector<FieldPattern> fields;
};

/// Tuple payload pattern - enum variant with tuple payload
struct TuplePayloadPattern {
    std::vector<PatternPtr> elements;
};

/// Record payload pattern - enum variant with record payload
struct RecordPayloadPattern {
    std::vector<FieldPattern> fields;
};

/// Enum payload pattern - either tuple or record form
using EnumPayloadPattern = std::variant<TuplePayloadPattern, RecordPayloadPattern>;

/// Enum pattern - destructures enum variants (Result::Ok(v))
struct EnumPattern {
    TypePath path;
    Identifier name;
    std::optional<EnumPayloadPattern> payload_opt;
};

/// Modal record payload - fields for modal state pattern
struct ModalRecordPayload {
    std::vector<FieldPattern> fields;
};

/// Modal pattern - matches modal state (@State{fields})
struct ModalPattern {
    Identifier state;
    std::optional<ModalRecordPayload> fields_opt;
};

// ---------------------------------------------------------------------------
// 3. RANGE PATTERN
// ---------------------------------------------------------------------------

/// Range pattern - matches ranges in patterns (0..10, 0..=10)
/// Note: Only Exclusive (..) and Inclusive (..=) are valid in patterns.
/// Other RangeKind values are expression-only.
struct RangePattern {
    RangeKind kind;
    PatternPtr lo;
    PatternPtr hi;
};

// ---------------------------------------------------------------------------
// 4. PATTERN NODE VARIANT
// ---------------------------------------------------------------------------

/// All possible pattern node types
using PatternNode = std::variant<
    LiteralPattern,
    WildcardPattern,
    IdentifierPattern,
    TypedPattern,
    SpliceExprNode,
    TuplePattern,
    RecordPattern,
    EnumPattern,
    ModalPattern,
    RangePattern
>;

// ---------------------------------------------------------------------------
// 5. PATTERN WRAPPER
// ---------------------------------------------------------------------------

/// Pattern with source span - the main pattern type used throughout the AST
struct Pattern {
    Span span;
    PatternNode node;
};

// ---------------------------------------------------------------------------
// 6. IF-IS CASE CLAUSE
// ---------------------------------------------------------------------------

/// If-case clause - a single case in an if-is case analysis expression
/// Defined here since it contains a pattern; used by IfCaseExpr in ast_exprs.h
struct IfCaseClause {
    std::shared_ptr<Pattern> pattern;
    ExprPtr body;
};

}  // namespace cursive::ast
