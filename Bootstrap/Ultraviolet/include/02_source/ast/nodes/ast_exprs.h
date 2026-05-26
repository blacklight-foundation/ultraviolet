// ===========================================================================
// ast_exprs.h - Expression AST Node Definitions
// ===========================================================================
//
// Expression nodes are the largest AST category with 50+ node types. This file
// contains all struct definitions for expression nodes in the Ultraviolet grammar,
// plus the ExprNode variant and Expr wrapper.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 5.2 and Appendix C.3
//
// Expr = (Span, ExprNode)
// ExprNode variants by category:
//
//   ERROR/BASIC:      ErrorExpr, LiteralExpr, IdentifierExpr
//   QUALIFIED:        QualifiedNameExpr, QualifiedApplyExpr, PathExpr
//   OPERATORS:        RangeExpr, BinaryExpr, UnaryExpr, CastExpr
//   MEMORY:           DerefExpr, AddressOfExpr, MoveExpr, AllocExpr, PtrNullExpr
//   AGGREGATE:        TupleExpr, ArrayExpr, ArrayRepeatExpr, RecordExpr, EnumLiteralExpr
//   INTRINSIC:        SizeofExpr, AlignofExpr
//   CONTROL FLOW:     IfExpr, IfCaseExpr, Loop*, BlockExpr, UnsafeBlockExpr
//   POSTFIX:          Field/Tuple/Index Access, CallExpr, MethodCallExpr, PropagateExpr
//   SPECIAL:          AttributedExpr, TransmuteExpr, ClosureExpr, PipelineExpr
//   CONTRACT:         ResultExpr, EntryExpr
//   ASYNC:            YieldExpr, YieldFromExpr, SyncExpr, RaceExpr, AllExpr
//   CONCURRENCY:      ParallelExpr, SpawnExpr, WaitExpr, FenceExpr, DispatchExpr
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
#include "02_source/ast/nodes/ast_attributes.h"
#include "02_source/ast/nodes/ast_patterns.h"

namespace ultraviolet::ast {

// Core aliases, helper types, and enums are defined in ast_common.h and ast_enums.h.

// ===========================================================================
// 1. BASIC EXPRESSIONS
// ===========================================================================

/// Error expression - sentinel for parse errors
struct ErrorExpr {};

/// Literal expression - integer, float, string, char, bool literals
struct LiteralExpr {
    Token literal;
};

/// Identifier expression - simple name reference
struct IdentifierExpr {
    Identifier name;
    bool from_splice = false;
};

/// Qualified name expression - module::name reference
struct QualifiedNameExpr {
    ModulePath path;
    Identifier name;
};

// ===========================================================================
// 2. QUALIFIED EXPRESSIONS
// ===========================================================================

/// Qualified apply expression - module::name(args) or module::name{fields}
struct QualifiedApplyExpr {
    ModulePath path;
    Identifier name;
    ApplyArgs args;
};

/// Path expression - module::name path reference
struct PathExpr {
    ModulePath path;
    Identifier name;
};

/// Enum literal expression - Enum::Variant or Enum::Variant(payload)
struct EnumLiteralExpr {
    Path path;
    std::optional<EnumPayload> payload_opt;
};

/// Compile-time type literal expression - Type::<T>
struct TypeLiteralExpr {
    TypePtr type;
};

/// Compile-time quote expression - quote { ... }, quote type { ... }, quote pattern { ... }
struct QuoteExpr {
    QuoteKind kind = QuoteKind::Unspecified;
    std::vector<Token> tokens;
};

// ===========================================================================
// 3. OPERATOR EXPRESSIONS
// ===========================================================================

/// Range expression - a..b, a..=b, a.., ..b, ..=b, ..
struct RangeExpr {
    RangeKind kind;
    ExprPtr lhs;  // May be null for open start (..b)
    ExprPtr rhs;  // May be null for open end (a..)
};

/// Binary expression - a + b, a && b, etc.
struct BinaryExpr {
    Identifier op;  // Operator as string ("+", "-", "&&", etc.)
    ExprPtr lhs;
    ExprPtr rhs;
};

/// Cast expression - expr as Type
struct CastExpr {
    ExprPtr value;
    TypePtr type;
};

/// Unary expression - -a, !a
struct UnaryExpr {
    Identifier op;  // Operator as string ("-", "!", etc.)
    ExprPtr value;
};

// ===========================================================================
// 4. MEMORY EXPRESSIONS
// ===========================================================================

/// Dereference expression - *ptr
struct DerefExpr {
    ExprPtr value;
};

/// Address-of expression - &place
struct AddressOfExpr {
    ExprPtr place;
};

/// Move expression - move x
struct MoveExpr {
    ExprPtr place;
};

/// Copy expression - copy x
struct CopyExpr {
    ExprPtr value;
};

/// Allocation expression - ^value or region^value
struct AllocExpr {
    std::optional<Identifier> region_opt;
    ExprPtr value;
};

/// Null pointer expression - Ptr::null()
struct PtrNullExpr {};

// ===========================================================================
// 5. AGGREGATE EXPRESSIONS
// ===========================================================================

/// Tuple expression - (a, b, c) or (a;) for single element
struct TupleExpr {
    std::vector<ExprPtr> elements;
};

/// Array literal segment - one explicit element
struct ArrayElemSegment {
    ExprPtr value;

    ArrayElemSegment() = default;
    ArrayElemSegment(ExprPtr init_value) : value(std::move(init_value)) {}
};

/// Array literal segment - repeated element [value; count]
struct ArrayRepeatSegment {
    ExprPtr value;
    ExprPtr count;
};

using ArraySegment = std::variant<ArrayElemSegment, ArrayRepeatSegment>;

/// Array expression - [a, b, c] or mixed segmented form [0; 4, 1, 0; 22]
struct ArrayExpr {
    std::vector<ArraySegment> elements;
};

/// Array repeat expression - [value; count]
struct ArrayRepeatExpr {
    ExprPtr value;
    ExprPtr count;
};

template <typename Fn>
void ForEachArrayExprSubexpr(const ArrayExpr& expr, Fn&& fn) {
    for (const auto& segment : expr.elements) {
        std::visit(
            [&](const auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ArrayElemSegment>) {
                    if (node.value) {
                        fn(node.value);
                    }
                } else if constexpr (std::is_same_v<T, ArrayRepeatSegment>) {
                    if (node.value) {
                        fn(node.value);
                    }
                    if (node.count) {
                        fn(node.count);
                    }
                }
            },
            segment);
    }
}

template <typename Fn>
void ForEachArrayExprSubexpr(ArrayExpr& expr, Fn&& fn) {
    for (auto& segment : expr.elements) {
        std::visit(
            [&](auto& node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ArrayElemSegment>) {
                    if (node.value) {
                        fn(node.value);
                    }
                } else if constexpr (std::is_same_v<T, ArrayRepeatSegment>) {
                    if (node.value) {
                        fn(node.value);
                    }
                    if (node.count) {
                        fn(node.count);
                    }
                }
            },
            segment);
    }
}

// ===========================================================================
// 6. INTRINSIC EXPRESSIONS
// ===========================================================================

/// Sizeof expression - sizeof(Type)
struct SizeofExpr {
    TypePtr type;
};

/// Alignof expression - alignof(Type)
struct AlignofExpr {
    TypePtr type;
};

// ===========================================================================
// 7. RECORD EXPRESSION
// ===========================================================================

/// Record literal expression - Type{ x: 1, y: 2 }
/// Target can be a plain type path or a modal state reference.
struct RecordExpr {
    std::variant<TypePath, ModalStateRef> target;
    std::vector<FieldInit> fields;
};

// ===========================================================================
// 8. CONTROL FLOW EXPRESSIONS
// ===========================================================================

/// If expression - if cond { then } else { else }
struct IfExpr {
    ExprPtr cond;
    ExprPtr then_expr;
    ExprPtr else_expr;  // May be null for if without else
};

/// If-is single-pattern analysis expression.
struct IfIsExpr {
    ExprPtr scrutinee;
    PatternPtr pattern;
    ExprPtr then_expr;
    ExprPtr else_expr;  // May be null for if without else
};

/// If-is case-list analysis expression.
struct IfCaseExpr {
    ExprPtr scrutinee;
    std::vector<IfCaseClause> cases;
    ExprPtr else_expr;  // May be null when analysis is statically exhaustive
};

// LoopInvariant is defined in ast_common.h

/// Infinite loop expression - loop { body }
struct LoopInfiniteExpr {
    std::optional<LoopInvariant> invariant_opt;
    BlockPtr body;
};

/// Conditional loop expression - loop condition { body }
struct LoopConditionalExpr {
    ExprPtr cond;
    std::optional<LoopInvariant> invariant_opt;
    BlockPtr body;
};

/// Iterator loop expression - loop pattern in iter { body }
struct LoopIterExpr {
    PatternPtr pattern;
    TypePtr type_opt;  // Optional type annotation
    ExprPtr iter;
    std::optional<LoopInvariant> invariant_opt;
    BlockPtr body;
};

// ===========================================================================
// 9. BLOCK EXPRESSIONS
// ===========================================================================

/// Block expression - { stmts; tail_expr? }
struct BlockExpr {
    BlockPtr block;
};

/// Unsafe block expression - unsafe { stmts }
struct UnsafeBlockExpr {
    BlockPtr block;
};

/// Compile-time expression - comptime { expr }
struct ComptimeExpr {
    ExprPtr body;
    AttrOpt attrs_opt;
};

/// Compile-time if expression - comptime if cond { ... } else { ... }
struct CtIfExpr {
    ExprPtr cond;
    BlockPtr then_block;
    BlockPtr else_block_opt;  // May be null
};

/// Compile-time iterator loop - comptime loop pattern (: type)? in iter { body }
struct CtLoopIterExpr {
    PatternPtr pattern;
    TypePtr type_opt;  // Optional type annotation
    ExprPtr iter;
    BlockPtr body;
};

/// Attributed expression - #attr expr (e.g., #dynamic contract)
struct AttributedExpr {
    AttributeList attrs;
    ExprPtr expr;
};

/// Transmute expression - transmute<From, To>(value)
struct TransmuteExpr {
    TypePtr from;
    TypePtr to;
    ExprPtr value;
};

// ===========================================================================
// 9.5. CLOSURE EXPRESSION
// ===========================================================================

/// Closure parameter - move? identifier (: type)?
struct ClosureParam {
    bool move_capture = false;
    Identifier name;
    TypePtr type_opt;  // Optional type annotation
};

/// Closure expression - |params| -> type body
struct ClosureExpr {
    std::vector<ClosureParam> params;
    TypePtr ret_type_opt;  // Optional return type annotation
    ExprPtr body;
};

// ===========================================================================
// 9.6. PIPELINE EXPRESSION
// ===========================================================================

/// Pipeline expression - lhs => rhs
struct PipelineExpr {
    ExprPtr lhs;
    ExprPtr rhs;
};

// ===========================================================================
// 10. POSTFIX EXPRESSIONS
// ===========================================================================

/// Field access expression - base.field
struct FieldAccessExpr {
    ExprPtr base;
    Identifier name;
};

/// Tuple access expression - base.0
struct TupleAccessExpr {
    ExprPtr base;
    TupleIndex index;  // Parsed integer value
};

/// Index access expression - base[index]
struct IndexAccessExpr {
    ExprPtr base;
    ExprPtr index;
};

/// Call expression - callee<generics>(args)
struct CallExpr {
    ExprPtr callee;
    std::vector<TypePtr> generic_args;
    std::vector<Arg> args;
};

/// Call expression with explicit type arguments - callee<T1, T2>(args)
struct CallTypeArgsExpr {
    ExprPtr callee;
    std::vector<TypePtr> type_args;
    std::vector<Arg> args;
};

/// Method call expression - receiver~>method(args)
struct MethodCallExpr {
    ExprPtr receiver;
    Identifier name;
    std::vector<Arg> args;
};

/// Propagate expression - expr? (union propagation)
struct PropagateExpr {
    ExprPtr value;
};

// ===========================================================================
// 11. CONTRACT EXPRESSIONS
// ===========================================================================

/// Result expression - @result (return value in postconditions)
struct ResultExpr {};

/// Entry expression - @entry(expr) (value at procedure entry)
struct EntryExpr {
    ExprPtr expr;
};

// ===========================================================================
// 12. ASYNC EXPRESSIONS
// ===========================================================================

/// Yield expression - yield value or yield release value
struct YieldExpr {
    bool release = false;  // true for yield release
    ExprPtr value;
};

/// Yield from expression - yield from expr or yield release from expr
struct YieldFromExpr {
    bool release = false;  // true for yield release from
    ExprPtr value;
};

/// Sync expression - sync expr (run async to completion)
struct SyncExpr {
    ExprPtr value;
};

/// Race handler - what to do when a race arm completes
struct RaceHandler {
    RaceHandlerKind kind = RaceHandlerKind::Return;
    ExprPtr value;
};

/// Race arm - one alternative in a race expression
struct RaceArm {
    ExprPtr expr;
    PatternPtr pattern;
    RaceHandler handler;
};

/// Race expression - race { arms } (first completion wins)
struct RaceExpr {
    std::vector<RaceArm> arms;
};

/// All expression - all { exprs } (wait for all completions)
struct AllExpr {
    std::vector<ExprPtr> exprs;
};

// ===========================================================================
// 13. CONCURRENCY EXPRESSIONS
// ===========================================================================

/// Parallel block option - cancel:, name:, workgroup:, or workgroups:
struct ParallelOption {
    ParallelOptionKind kind;
    ExprPtr value;
    Span span;
};

/// Parallel expression - parallel domain [opts] { body }
struct ParallelExpr {
    ExprPtr domain;  // $ExecutionDomain
    std::vector<ParallelOption> opts;
    BlockPtr body;
};

/// Spawn option - name:, affinity:, priority:, or move capture
struct SpawnOption {
    SpawnOptionKind kind;
    ExprPtr value;
    Span span;
};

/// Spawn expression - spawn [opts] { body }
struct SpawnExpr {
    std::vector<SpawnOption> opts;
    BlockPtr body;
};

/// Wait expression - wait handle
struct WaitExpr {
    ExprPtr handle;  // Spawned<T>
};

/// Fence expression - fence(order)
struct FenceExpr {
    FenceOrder order = FenceOrder::SeqCst;
};

/// Dispatch option - reduce:, ordered, chunk:, or workgroup:
struct DispatchOption {
    DispatchOptionKind kind;
    ReduceOp reduce_op = ReduceOp::Add;    // For Reduce kind
    Identifier custom_reduce_name;          // For Custom reduce
    ExprPtr chunk_expr;                     // For Chunk kind
    ExprPtr workgroup_expr;                 // For Workgroup kind
    Span span;
};

/// Dispatch key clause - key path_expr mode
struct DispatchKeyClause {
    KeyPathExpr key_path;
    KeyMode mode;
    Span span;
};

/// Dispatch expression - dispatch i in range [opts] { body }
struct DispatchExpr {
    PatternPtr pattern;  // Loop variable pattern
    ExprPtr range;       // Range<I>
    std::optional<DispatchKeyClause> key_clause;
    std::vector<DispatchOption> opts;
    BlockPtr body;
};

// ===========================================================================
// EXPRESSION NODE VARIANT
// ===========================================================================

/// ExprNode is a variant holding all possible expression node kinds.
/// The Expr wrapper pairs this with a source span for error reporting.
using ExprNode = std::variant<
    // Error/Basic
    ErrorExpr,
    LiteralExpr,
    IdentifierExpr,
    QualifiedNameExpr,
    // Qualified
    QualifiedApplyExpr,
    PathExpr,
    // Operators
    RangeExpr,
    BinaryExpr,
    CastExpr,
    UnaryExpr,
    // Memory
    DerefExpr,
    AddressOfExpr,
    MoveExpr,
    CopyExpr,
    AllocExpr,
    PtrNullExpr,
    // Aggregate
    TupleExpr,
    ArrayExpr,
    ArrayRepeatExpr,
    SizeofExpr,
    AlignofExpr,
    RecordExpr,
    EnumLiteralExpr,
    TypeLiteralExpr,
    QuoteExpr,
    SpliceExprNode,
    SpliceIdentNode,
    // Control flow
    IfExpr,
    IfIsExpr,
    IfCaseExpr,
    LoopInfiniteExpr,
    LoopConditionalExpr,
    LoopIterExpr,
    BlockExpr,
    UnsafeBlockExpr,
    ComptimeExpr,
    CtIfExpr,
    CtLoopIterExpr,
    // Special
    AttributedExpr,
    TransmuteExpr,
    // Closure
    ClosureExpr,
    // Pipeline
    PipelineExpr,
    // Postfix
    FieldAccessExpr,
    TupleAccessExpr,
    IndexAccessExpr,
    CallExpr,
    CallTypeArgsExpr,
    MethodCallExpr,
    PropagateExpr,
    // Contract
    ResultExpr,
    EntryExpr,
    // Async
    YieldExpr,
    YieldFromExpr,
    SyncExpr,
    RaceExpr,
    AllExpr,
    // Concurrency
    ParallelExpr,
    SpawnExpr,
    WaitExpr,
    FenceExpr,
    DispatchExpr
>;

// ===========================================================================
// EXPRESSION WRAPPER
// ===========================================================================

/// Expr wraps an ExprNode with source location information.
/// Spec: Expr = (Span, ExprNode)
struct Expr {
    Span span;
    ExprNode node;
};

inline AttrOpt ExprAttrs(const Expr& expr) {
    if (const auto* comptime = std::get_if<ComptimeExpr>(&expr.node)) {
        return comptime->attrs_opt;
    }
    if (const auto* attributed = std::get_if<AttributedExpr>(&expr.node)) {
        return attributed->attrs;
    }
    return std::nullopt;
}

inline ExprPtr AttachExprAttrs(const ExprPtr& expr, AttributeList attrs,
                               const Span& span) {
    if (!expr || attrs.empty()) {
        return expr;
    }

    if (const auto* comptime = std::get_if<ComptimeExpr>(&expr->node)) {
        ComptimeExpr merged = *comptime;
        AttributeList combined = std::move(attrs);
        const AttributeList& existing = AttrListOf(merged.attrs_opt);
        combined.insert(combined.end(), existing.begin(), existing.end());
        merged.attrs_opt = std::move(combined);
        return std::make_shared<Expr>(Expr{span, std::move(merged)});
    }

    if (const auto* attributed = std::get_if<AttributedExpr>(&expr->node)) {
        AttributeList combined = std::move(attrs);
        combined.insert(combined.end(), attributed->attrs.begin(),
                        attributed->attrs.end());
        if (attributed->expr &&
            std::holds_alternative<ComptimeExpr>(attributed->expr->node)) {
            return AttachExprAttrs(attributed->expr, std::move(combined), span);
        }
        AttributedExpr merged;
        merged.attrs = std::move(combined);
        merged.expr = attributed->expr;
        return std::make_shared<Expr>(Expr{span, std::move(merged)});
    }

    AttributedExpr attributed;
    attributed.attrs = std::move(attrs);
    attributed.expr = expr;
    return std::make_shared<Expr>(Expr{span, std::move(attributed)});
}

inline ExprPtr AttachExprAttrs(const ExprPtr& expr, AttributeList attrs) {
    if (!expr) {
        return expr;
    }
    return AttachExprAttrs(expr, std::move(attrs), expr->span);
}

inline const AttributeList& ExprAttrList(const Expr& expr) {
    if (const auto* comptime = std::get_if<ComptimeExpr>(&expr.node)) {
        return AttrListOf(comptime->attrs_opt);
    }
    if (const auto* attributed = std::get_if<AttributedExpr>(&expr.node)) {
        return attributed->attrs;
    }
    return EmptyAttributeList();
}

inline AttributeList ExprAttrByName(const Expr& expr, std::string_view name) {
    return AttrByName(ExprAttrList(expr), name);
}

inline bool DynamicExpr(const Expr& expr) {
    return !ExprAttrByName(expr, "dynamic").empty();
}

}  // namespace ultraviolet::ast
