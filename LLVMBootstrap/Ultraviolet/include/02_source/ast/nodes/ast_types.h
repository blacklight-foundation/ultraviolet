// ===========================================================================
// ast_types.h - Type AST Node Definitions
// ===========================================================================
//
// This file contains all struct definitions for type nodes in the Ultraviolet
// grammar, plus the TypeNode variant and Type wrapper.
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.2.3 - Type Nodes
//
// Type = (Span, TypeNode)
// TypeNode variants:
//   TypePrim    - primitive type name
//   TypePerm    - permission-qualified type (const/unique/shared T)
//   TypeUnion   - union type (T1 | T2)
//   TypeFunc    - function type ((T1, T2) -> R)
//   TypeTuple   - tuple type ((T1, T2))
//   TypeArray   - fixed array type ([T; n])
//   TypeSlice   - slice type ([T])
//   TypeSafePtr - safe pointer type (Ptr<T>@State)
//   TypeRawPtr  - raw pointer type (*imm T, *mut T)
//   TypeString  - string type (string@State)
//   TypeBytes   - bytes type (bytes@State)
//   TypeDynamic - dynamic class type ($ClassName)
//   TypeModal   - modal state type (Modal@State)
//   TypePath    - named type path (Module::Type)
//   TypeOpaque  - opaque type (opaque Path)
//   TypeRefine  - refinement type (T |: { pred })
//
// ===========================================================================

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast_common.h"

namespace ultraviolet::ast {

// Core AST aliases and enums are defined in ast_common.h / ast_enums.h.

// ===========================================================================
// Primitive/Basic Type Nodes
// ===========================================================================

// Primitive type: i32, bool, char, etc.
// Syntax: identifier
struct TypePrim {
    Identifier name;
};

// Permission-qualified type: const T, unique T, shared T
// Syntax: permission type
struct TypePermType {
    TypePerm perm;
    std::shared_ptr<Type> base;
};

// ===========================================================================
// Compound Type Nodes
// ===========================================================================

// Union type: T1 | T2
// Syntax: type "|" type (unordered - A | B is equivalent to B | A)
struct TypeUnion {
    std::vector<std::shared_ptr<Type>> types;
};

// Function parameter with optional mode
// Syntax: (move? type)
struct TypeFuncParam {
    std::optional<ParamMode> mode;
    std::shared_ptr<Type> type;
};

// Function type: (T1, T2) -> R
// Syntax: "(" param_list? ")" "->" type
struct TypeFunc {
    std::vector<TypeFuncParam> params;
    std::shared_ptr<Type> ret;
};

// Closure dependency entry: name: Type
struct SharedDep {
    Identifier name;
    std::shared_ptr<Type> type;
};

// Closure type: |params| -> Ret [shared: {deps}]
// params is a list of (move?, type) entries
struct TypeClosure {
    std::vector<TypeFuncParam> params;
    std::shared_ptr<Type> ret;
    std::optional<std::vector<SharedDep>> deps_opt;
};

// Tuple type: (T1, T2) or (T;) for single-element
// Syntax: "(" type_list ")" or "(" type ";" ")"
struct TypeTuple {
    std::vector<std::shared_ptr<Type>> elements;
};

// ===========================================================================
// Collection Type Nodes
// ===========================================================================

// Fixed array type: [T; n]
// Syntax: "[" type ";" expr "]"
struct TypeArray {
    std::shared_ptr<Type> element;
    ExprPtr length;
};

// Slice type: [T]
// Syntax: "[" type "]"
struct TypeSlice {
    std::shared_ptr<Type> element;
};

// ===========================================================================
// Pointer Type Nodes
// ===========================================================================

// Safe pointer type: Ptr<T>@Valid, Ptr<T>@Null
// Syntax: "Ptr" "<" type ">" "@" state?
struct TypeSafePtr {
    std::shared_ptr<Type> element;
    std::optional<PtrState> state;
};

// Raw pointer type: *imm T, *mut T
// Syntax: "*" qual type
struct TypeRawPtr {
    RawPtrQual qual;
    std::shared_ptr<Type> element;
};

// ===========================================================================
// String/Bytes Type Nodes
// ===========================================================================

// String type: string@Managed, string@View
// Syntax: "string" "@" state?
struct TypeString {
    std::optional<StringState> state;
};

// Bytes type: bytes@Managed, bytes@View
// Syntax: "bytes" "@" state?
struct TypeBytes {
    std::optional<BytesState> state;
};

// ===========================================================================
// Named Type Nodes
// ===========================================================================

// Dynamic class type: $ClassName
// Syntax: "$" path
struct TypeDynamic {
    TypePath path;
};

// Named type path with generic arguments: Foo, Bar<T, U>
// Syntax: path generic_args?
struct TypePathType {
    TypePath path;
    std::vector<std::shared_ptr<Type>> generic_args;
};

// Type application: Path<Args>
// Syntax: path "<" type_list ">"
struct TypeApply {
    TypePath path;
    std::vector<std::shared_ptr<Type>> args;
};

using TypeModalRef = std::variant<TypePathType, TypeApply>;

inline TypeModalRef MakeTypeModalRef(
    TypePath path,
    std::vector<std::shared_ptr<Type>> generic_args) {
    if (generic_args.empty()) {
        TypePathType path_type;
        path_type.path = std::move(path);
        return TypeModalRef{std::move(path_type)};
    }

    TypeApply apply;
    apply.path = std::move(path);
    apply.args = std::move(generic_args);
    return TypeModalRef{std::move(apply)};
}

inline const TypePath& TypeModalRefPath(const TypeModalRef& modal_ref) {
    return std::visit(
        [](const auto& node) -> const TypePath& {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, TypePathType>) {
                if (node.generic_args.empty()) {
                    SPEC_RULE("ModalRefPath(TypePath(p))");
                }
            } else {
                SPEC_RULE("ModalRefPath(TypeApply(p, _))");
            }
            return node.path;
        },
        modal_ref);
}

inline const std::vector<std::shared_ptr<Type>>& TypeModalRefArgs(
    const TypeModalRef& modal_ref) {
    static const std::vector<std::shared_ptr<Type>> kEmptyArgs;
    return std::visit(
        [&](const auto& node) -> const std::vector<std::shared_ptr<Type>>& {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, TypePathType>) {
                if (node.generic_args.empty()) {
                    SPEC_RULE("ModalRefArgs(TypePath(_))");
                    return kEmptyArgs;
                }
                return node.generic_args;
            } else {
                SPEC_RULE("ModalRefArgs(TypeApply(_, args))");
                return node.args;
            }
        },
        modal_ref);
}

// Modal state type: Connection@Connected, Modal@State<T, U>
// Syntax: path generic_args? "@" state
struct TypeModalState {
    TypeModalRef modal_ref;
    TypePath path;
    std::vector<std::shared_ptr<Type>> generic_args;
    Identifier state;
};

inline void SyncTypeModalStateFromModalRef(TypeModalState& state) {
    state.path = TypeModalRefPath(state.modal_ref);
    state.generic_args = TypeModalRefArgs(state.modal_ref);
}

inline void SyncTypeModalStateFromFields(TypeModalState& state) {
    state.modal_ref = MakeTypeModalRef(state.path, state.generic_args);
}

// Opaque type: opaque Path
// Syntax: "opaque" path
struct TypeOpaque {
    TypePath path;
};

// ===========================================================================
// Advanced Type Nodes
// ===========================================================================

// Refinement type: T |: { predicate }
// Syntax: type "|:" "{" expr "}"
struct TypeRefine {
    std::shared_ptr<Type> base;
    ExprPtr predicate;
};

// Range type family.
struct TypeRange {
    std::shared_ptr<Type> base;
};

struct TypeRangeInclusive {
    std::shared_ptr<Type> base;
};

struct TypeRangeFrom {
    std::shared_ptr<Type> base;
};

struct TypeRangeTo {
    std::shared_ptr<Type> base;
};

struct TypeRangeToInclusive {
    std::shared_ptr<Type> base;
};

struct TypeRangeFull {};

// ===========================================================================
// Type Node Variant
// ===========================================================================

// TypeNode is a variant holding all possible type node kinds.
// The Type wrapper pairs this with a source span for error reporting.
using TypeNode = std::variant<
    TypePrim,
    TypePermType,
    TypeUnion,
    TypeFunc,
    TypeClosure,
    TypeTuple,
    TypeArray,
    TypeSlice,
    TypeSafePtr,
    TypeRawPtr,
    TypeString,
    TypeBytes,
    TypeDynamic,
    TypeModalState,
    TypePathType,
    TypeApply,
    SpliceExprNode,
    TypeOpaque,
    TypeRefine,
    TypeRange,
    TypeRangeInclusive,
    TypeRangeFrom,
    TypeRangeTo,
    TypeRangeToInclusive,
    TypeRangeFull
>;

// ===========================================================================
// Type Wrapper
// ===========================================================================

// Type wraps a TypeNode with source location information.
// Spec: Type = (Span, TypeNode)
struct Type {
    core::Span span;
    TypeNode node;
};

}  // namespace ultraviolet::ast
