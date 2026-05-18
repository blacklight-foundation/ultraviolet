// ===========================================================================
// ast_common.h - Common AST types and utilities
// ===========================================================================
//
// PURPOSE:
//   Common types and utilities shared across AST node categories. Contains
//   helper types that don't fit in a single category and are used by
//   multiple AST node headers.
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.2 - AST Node Catalog
//
// ===========================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/int128.h"
#include "02_source/ast/nodes/ast_fwd.h"
#include "02_source/ast/nodes/ast_enums.h"
#include "00_core/span.h"
#include "02_source/lexer/token.h"

namespace ultraviolet::ast {

// Path aliases, core forward declarations, and pointer aliases live in ast_fwd.h.

// ===========================================================================
// Documentation
// ===========================================================================

// Reuse DocKind and DocComment from the lexer namespace.
using DocKind = ultraviolet::lexer::DocKind;
using DocComment = ultraviolet::lexer::DocComment;
using DocList = std::vector<DocComment>;

using Span = ultraviolet::core::Span;
using Token = ultraviolet::lexer::Token;
using TokenKind = ultraviolet::lexer::TokenKind;
using TupleIndex = ultraviolet::core::UInt128;

inline bool TupleIndexEqual(TupleIndex lhs, TupleIndex rhs) {
  return ultraviolet::core::UInt128High(lhs) == ultraviolet::core::UInt128High(rhs) &&
         ultraviolet::core::UInt128Low(lhs) == ultraviolet::core::UInt128Low(rhs);
}

inline std::optional<std::size_t> TupleIndexToSize(TupleIndex index) {
  if (!ultraviolet::core::UInt128FitsU64(index)) {
    return std::nullopt;
  }
  const std::uint64_t value = ultraviolet::core::UInt128ToU64(index);
  if (value > static_cast<std::uint64_t>(
                  std::numeric_limits<std::size_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(value);
}

inline std::string FormatTupleIndex(TupleIndex index) {
  const TupleIndex ten = ultraviolet::core::UInt128FromU64(10);
  TupleIndex value = index;
  std::string digits;

  while (true) {
    const TupleIndex quotient = ultraviolet::core::UInt128Div(value, ten);
    const TupleIndex remainder = ultraviolet::core::UInt128Sub(
        value, ultraviolet::core::UInt128Mul(quotient, ten));
    digits.push_back(static_cast<char>(
        '0' + ultraviolet::core::UInt128ToU64(remainder)));
    if (ultraviolet::core::UInt128FitsU64(quotient) &&
        ultraviolet::core::UInt128ToU64(quotient) == 0) {
      break;
    }
    value = quotient;
  }

  std::reverse(digits.begin(), digits.end());
  return digits;
}

// ===========================================================================
// Argument Types (shared by multiple expression kinds)
// ===========================================================================

// Arg represents a function/method call argument and its passing form.
// Used by: CallExpr, MethodCallExpr, QualifiedApplyExpr
enum class ArgPassKind {
  Ref,
  Move,
  Copy,
};

struct Arg {
  ArgPassKind pass = ArgPassKind::Ref;
  ExprPtr value;
  ultraviolet::core::Span span;
};

inline bool IsMoveArg(const Arg& arg) {
  return arg.pass == ArgPassKind::Move;
}

inline bool IsCopyArg(const Arg& arg) {
  return arg.pass == ArgPassKind::Copy;
}

inline bool IsRefArg(const Arg& arg) {
  return arg.pass == ArgPassKind::Ref;
}

// ParenArgs holds positional arguments in parentheses: f(a, b, c)
struct ParenArgs {
  std::vector<Arg> args;
};

// FieldInit represents a named field initialization: name: value
// Used by: RecordExpr, BraceArgs, EnumPayloadBrace
struct FieldInit {
  Identifier name;
  ExprPtr value;
  ultraviolet::core::Span span;
};

// BraceArgs holds named field arguments in braces: Point{ x: 1, y: 2 }
struct BraceArgs {
  std::vector<FieldInit> fields;
};

// ApplyArgs represents either paren or brace argument style
using ApplyArgs = std::variant<ParenArgs, BraceArgs>;

// ===========================================================================
// Enum Payload Types (shared by expressions and patterns)
// ===========================================================================

// Tuple-style enum payload: Variant(a, b, c)
struct EnumPayloadParen {
  std::vector<ExprPtr> elements;
};

// Record-style enum payload: Variant{ x: 1, y: 2 }
struct EnumPayloadBrace {
  std::vector<FieldInit> fields;
};

using EnumPayload = std::variant<EnumPayloadParen, EnumPayloadBrace>;

// ===========================================================================
// Key System Types (shared by expressions and statements)
// ===========================================================================
// The key system provides synchronized access to shared data.
// Key paths identify the data being accessed; key mode specifies read/write.

// KeySegField represents a field access segment in a key path: .name or #.name
struct KeySegField {
  bool marked = false;  // true if # boundary marker present
  Identifier name;
};

// KeySegIndex represents an index access segment in a key path: [expr] or #[expr]
struct KeySegIndex {
  bool marked = false;  // true if # boundary marker present
  ExprPtr expr;
};

// KeySeg is a single segment in a key path (field or index access)
using KeySeg = std::variant<KeySegField, KeySegIndex>;

// KeyPathExpr represents a complete key path: root.field1[idx].field2
// Used by: DispatchExpr (key clause), KeyBlockStmt
struct KeyPathExpr {
  Identifier root;
  std::vector<KeySeg> segs;
  ultraviolet::core::Span span;
};

// ===========================================================================
// Generic Type References (shared by expressions and types)
// ===========================================================================

// GenericTypeRef represents a type path with generic arguments: Foo<T, U>
struct GenericTypeRef {
  TypePath path;
  std::vector<TypePtr> generic_args;
};

using ModalRef = std::variant<TypePath, GenericTypeRef>;

inline ModalRef MakeModalRef(TypePath path, std::vector<TypePtr> generic_args) {
  if (generic_args.empty()) {
    return ModalRef{std::move(path)};
  }

  GenericTypeRef apply;
  apply.path = std::move(path);
  apply.generic_args = std::move(generic_args);
  return ModalRef{std::move(apply)};
}

inline const TypePath& ModalRefPath(const ModalRef& modal_ref) {
  return std::visit(
      [](const auto& node) -> const TypePath& {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePath>) {
          SPEC_RULE("ModalRefPath(TypePath(p))");
          return node;
        } else {
          SPEC_RULE("ModalRefPath(TypeApply(p, _))");
          return node.path;
        }
      },
      modal_ref);
}

inline const std::vector<TypePtr>& ModalRefArgs(const ModalRef& modal_ref) {
  static const std::vector<TypePtr> kEmptyArgs;
  return std::visit(
      [&](const auto& node) -> const std::vector<TypePtr>& {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePath>) {
          SPEC_RULE("ModalRefArgs(TypePath(_))");
          return kEmptyArgs;
        } else {
          SPEC_RULE("ModalRefArgs(TypeApply(_, args))");
          return node.generic_args;
        }
      },
      modal_ref);
}

// ModalStateRef represents a modal type in a specific state: Connection@Open<T>
struct ModalStateRef {
  ModalRef modal_ref;
  TypePath path;
  std::vector<TypePtr> generic_args;
  Identifier state;
};

inline void SyncModalStateRefFromModalRef(ModalStateRef& ref) {
  ref.path = ModalRefPath(ref.modal_ref);
  ref.generic_args = ModalRefArgs(ref.modal_ref);
}

inline void SyncModalStateRefFromFields(ModalStateRef& ref) {
  ref.modal_ref = MakeModalRef(ref.path, ref.generic_args);
}

// ===========================================================================
// Receiver Types (shared by method declarations)
// ===========================================================================

// ReceiverShorthand represents ~, ~!, or ~% receiver syntax.
struct ReceiverShorthand {
  ReceiverPerm perm;
};

// ReceiverExplicit represents an explicit receiver with optional mode and type.
struct ReceiverExplicit {
  std::optional<ParamMode> mode_opt;
  TypePtr type;
};

using Receiver = std::variant<ReceiverShorthand, ReceiverExplicit>;

// ===========================================================================
// Quote/Splice Helpers
// ===========================================================================

struct SpliceExprNode {
  ExprPtr expr;
  ultraviolet::core::Span span;
};

struct SpliceIdentNode {
  ExprPtr name_expr;
  ultraviolet::core::Span span;
};

// ===========================================================================
// Loop Invariant (shared by loop expressions)
// ===========================================================================

// LoopInvariant represents: where { predicate }
// Used by: LoopInfiniteExpr, LoopConditionalExpr, LoopIterExpr
struct LoopInvariant {
  ExprPtr predicate;
  ultraviolet::core::Span span;
};

}  // namespace ultraviolet::ast
