#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>

#include "00_core/span.h"

namespace cursive::ast {
struct Type;
struct Expr;
}

namespace cursive::analysis {

enum class Permission {
  Const,
  Unique,
  Shared,
};

bool IsPermInC0(Permission perm);

std::optional<Permission> ParsePermissionKeyword(std::string_view token);

enum class ParamMode {
  Move,
};

enum class RawPtrQual {
  Imm,
  Mut,
};

enum class StringState {
  Managed,
  View,
};

enum class BytesState {
  Managed,
  View,
};

enum class PtrState {
  Valid,
  Null,
  Expired,
};

using TypePath = std::vector<std::string>;

struct Type;
using TypeRef = std::shared_ptr<Type>;

struct TypePrim {
  std::string name;
};

struct TypeVar {
  std::uint32_t id = 0;
};

struct TypeRange {
  TypeRef base;
};

struct TypeRangeInclusive {
  TypeRef base;
};

struct TypeRangeFrom {
  TypeRef base;
};

struct TypeRangeTo {
  TypeRef base;
};

struct TypeRangeToInclusive {
  TypeRef base;
};

struct TypeRangeFull {};

struct TypePerm {
  Permission perm;
  TypeRef base;
};

struct TypeUnion {
  std::vector<TypeRef> members;
};

struct TypeFuncParam {
  std::optional<ParamMode> mode;
  TypeRef type;
};

struct TypeFunc {
  std::vector<TypeFuncParam> params;
  TypeRef ret;
};

struct TypeTuple {
  std::vector<TypeRef> elements;
};

struct TypeArray {
  TypeRef element;
  std::uint64_t length = 0;
  std::optional<std::string> length_expr_text;
};

struct TypeSlice {
  TypeRef element;
};

struct TypePtr {
  TypeRef element;
  std::optional<PtrState> state;
};

struct TypeRawPtr {
  RawPtrQual qual;
  TypeRef element;
};

struct TypeString {
  std::optional<StringState> state;
};

struct TypeBytes {
  std::optional<BytesState> state;
};

struct TypeDynamic {
  TypePath path;
};

struct TypeApply {
  TypePath path;
  std::vector<TypeRef> args;
};

using ModalRef = std::variant<TypePath, TypeApply>;

ModalRef MakeModalRef(TypePath path, std::vector<TypeRef> args = {});
const TypePath& ModalRefPath(const ModalRef& modal_ref);
const std::vector<TypeRef>& ModalRefArgs(const ModalRef& modal_ref);
TypeRef ModalRefType(const ModalRef& modal_ref);

struct ModalStateRef {
  ModalRef modal_ref;
  std::string state;
};

struct TypeModalState {
  ModalRef modal_ref;
  TypePath path;
  std::string state;
  std::vector<TypeRef> generic_args;
};

inline void SyncTypeModalStateFromModalRef(TypeModalState& state) {
  state.path = ModalRefPath(state.modal_ref);
  state.generic_args = ModalRefArgs(state.modal_ref);
}

inline void SyncTypeModalStateFromFields(TypeModalState& state) {
  state.modal_ref = MakeModalRef(state.path, state.generic_args);
}

struct TypePathType {
  TypePath path;
  std::vector<TypeRef> generic_args;  // C0X Extension: Foo<T, U>
};

struct TypeOpaque {
  TypePath class_path;
  const ast::Type* origin = nullptr;
  core::Span origin_span;
};

struct TypeRefine {
  TypeRef base;
  std::shared_ptr<ast::Expr> predicate;
};

// TypeClosure per CursiveSpecification.md Section 5.2
// TypeClosure = ⟨params, ret, deps_opt⟩
// params is vector of (move_mode, type) pairs
struct SharedDep {
  std::string name;
  TypeRef type;
};

struct TypeClosure {
  std::vector<std::pair<bool, TypeRef>> params;  // (is_move, type)
  TypeRef ret;
  std::optional<std::vector<SharedDep>> deps_opt;
};

using TypeNode = std::variant<TypePrim,
                              TypeVar,
                              TypePerm,
                              TypeUnion,
                              TypeFunc,
                              TypeTuple,
                              TypeArray,
                              TypeSlice,
                              TypePtr,
                              TypeRawPtr,
                              TypeString,
                              TypeBytes,
                              TypeDynamic,
                              TypeModalState,
                              TypeApply,
                              TypePathType,
                              TypeOpaque,
                              TypeRefine,
                              TypeRange,
                              TypeRangeInclusive,
                              TypeRangeFrom,
                              TypeRangeTo,
                              TypeRangeToInclusive,
                              TypeRangeFull,
                              TypeClosure>;

struct Type {
  TypeNode node;
};

inline const TypePath* AppliedTypePath(const Type& type) {
  if (const auto* path = std::get_if<TypePathType>(&type.node)) {
    return &path->path;
  }
  if (const auto* apply = std::get_if<TypeApply>(&type.node)) {
    return &apply->path;
  }
  return nullptr;
}

inline const std::vector<TypeRef>* AppliedTypeArgs(const Type& type) {
  if (const auto* path = std::get_if<TypePathType>(&type.node)) {
    return &path->generic_args;
  }
  if (const auto* apply = std::get_if<TypeApply>(&type.node)) {
    return &apply->args;
  }
  return nullptr;
}

TypeRef MakeType(TypeNode node);
TypeRef MakeTypePrim(std::string name);
TypeRef MakeTypeVar(std::uint32_t id);
TypeRef MakeTypeRange(TypeRef base);
TypeRef MakeTypeRangeInclusive(TypeRef base);
TypeRef MakeTypeRangeFrom(TypeRef base);
TypeRef MakeTypeRangeTo(TypeRef base);
TypeRef MakeTypeRangeToInclusive(TypeRef base);
TypeRef MakeTypeRangeFull();
TypeRef MakeTypePerm(Permission perm, TypeRef base);
TypeRef MakeTypeUnion(std::vector<TypeRef> members);
TypeRef MakeTypeFunc(std::vector<TypeFuncParam> params, TypeRef ret);
TypeRef MakeTypeTuple(std::vector<TypeRef> elements);
TypeRef MakeTypeArray(TypeRef element,
                      std::uint64_t length,
                      std::optional<std::string> length_expr_text = std::nullopt);
TypeRef MakeTypeSlice(TypeRef element);
TypeRef MakeTypePtr(TypeRef element, std::optional<PtrState> state);
TypeRef MakeTypeRawPtr(RawPtrQual qual, TypeRef element);
TypeRef MakeTypeString(std::optional<StringState> state);
TypeRef MakeTypeBytes(std::optional<BytesState> state);
TypeRef MakeTypeDynamic(TypePath path);
TypeRef MakeTypeModalState(ModalRef modal_ref, std::string state);
TypeRef MakeTypeModalState(TypePath path,
                           std::string state,
                           std::vector<TypeRef> generic_args = {});
TypeRef MakeTypePath(TypePath path);
TypeRef MakeTypePath(TypePath path, std::vector<TypeRef> generic_args);
TypePath SelfVarPath();
TypeRef SelfVarType();
bool IsSelfVarPath(const TypePath& path);
TypeRef MakeTypeApply(TypePath path, std::vector<TypeRef> args);
TypeRef MakeTypeOpaque(TypePath class_path,
                       const ast::Type* origin,
                       const core::Span& origin_span);
TypeRef MakeTypeRefine(TypeRef base,
                       std::shared_ptr<ast::Expr> predicate);
TypeRef MakeTypeClosure(std::vector<std::pair<bool, TypeRef>> params,
                        TypeRef ret,
                        std::optional<std::vector<SharedDep>> deps_opt);

std::string TypeToString(const Type& type);
std::string TypeToString(const TypeRef& type);

bool IsRangeType(const TypeRef& type);
std::optional<TypeRef> RangeElementType(const TypeRef& type);
bool IsRangeIndexType(const TypeRef& type);

struct TypeKey;

struct KeyAtom {
  enum class Kind {
    Number,
    String,
    Key,
    KeyList,
  };

  Kind kind = Kind::Number;
  std::uint64_t number = 0;
  std::string text;
  std::shared_ptr<TypeKey> key;
  std::vector<std::shared_ptr<TypeKey>> key_list;

  static KeyAtom Number(std::uint64_t value);
  static KeyAtom String(std::string value);
  static KeyAtom Key(std::shared_ptr<TypeKey> value);
  static KeyAtom KeyList(std::vector<std::shared_ptr<TypeKey>> value);
};

struct TypeKey {
  std::vector<KeyAtom> atoms;
};

TypeKey TypeKeyOf(const Type& type);
TypeKey TypeKeyOf(const TypeRef& type);
bool TypeKeyEqual(const TypeKey& lhs, const TypeKey& rhs);
bool TypeKeyLess(const TypeKey& lhs, const TypeKey& rhs);

std::vector<TypeRef> SortUnionMembers(const std::vector<TypeRef>& members);

std::vector<TypePath> TypePaths(const Type& type);
std::vector<TypePath> TypePaths(const TypeRef& type);

// C0X Extension: Async type helpers (§19.1)

/// Returns true if the type is an async type (Async<...> or a type alias thereof).
/// This checks for TypePathType or TypeModalState with path ["Async"], or
/// type aliases Future, Sequence, Stream, Pipe, Exchange.
bool IsAsyncType(const TypeRef& type);

/// Helper to compare identifiers for equality (case-sensitive).
bool IdEq(const std::string& a, const std::string& b);

/// Returns true for the canonical Async modal path: ["Async"].
bool IsAsyncModalPath(const TypePath& path);

/// Returns true for built-in async alias paths:
/// ["Future"], ["Sequence"], ["Stream"], ["Pipe"], ["Exchange"].
bool IsAsyncAliasPath(const TypePath& path);

/// Async type signature components.
/// For Async<Out, In, Result, E>, holds the extracted type arguments.
struct AsyncSig {
  TypeRef out;     // Yielded output type
  TypeRef in;      // Resume input type
  TypeRef result;  // Completion result type
  TypeRef err;     // Error type (! for infallible)
};

/// Extracts the async signature from an async type.
/// Returns nullopt if the type is not an async type.
/// Handles Async<Out, In, Result, E> and aliases:
///   Future<T; E = !> = Async<(), (), T, E>
///   Sequence<T> = Async<T, (), (), !>
///   Stream<T; E> = Async<T, (), (), E>
///   Pipe<In; Out> = Async<Out, In, (), !>
///   Exchange<T> = Async<T, T, T, !>
std::optional<AsyncSig> GetAsyncSig(const TypeRef& type);

}  // namespace cursive::analysis
