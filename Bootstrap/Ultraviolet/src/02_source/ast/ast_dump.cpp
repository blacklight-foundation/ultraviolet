// ===========================================================================
// ast_dump.cpp - AST Pretty-Printing and Debugging Utilities
// ===========================================================================
//
// PURPOSE:
//   AST pretty-printing and debugging utilities. Provides functions to
//   serialize AST nodes to human-readable text format for debugging and
//   diagnostic output.
//
// ===========================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.2 (AST Node Catalog)
// ===========================================================================
//   No direct spec reference for AST dumping (implementation detail).
//   The AST structure defined in Section 3.3.2 dictates what information
//   should be included in dumps.
//
// ===========================================================================

#include "02_source/ast/ast_dump.h"
#include "02_source/ast/ast_utils.h"

#include <ostream>
#include <sstream>
#include <string>
#include <variant>

namespace ultraviolet::ast {

// ===========================================================================
// ENUM TO STRING CONVERSION
// ===========================================================================

const char* to_string(TypePerm perm) {
  switch (perm) {
    case TypePerm::Const: return "const";
    case TypePerm::Unique: return "unique";
    case TypePerm::Shared: return "shared";
  }
  return "unknown";
}

const char* to_string(RawPtrQual qual) {
  switch (qual) {
    case RawPtrQual::Imm: return "imm";
    case RawPtrQual::Mut: return "mut";
  }
  return "unknown";
}

const char* to_string(PtrState state) {
  switch (state) {
    case PtrState::Valid: return "Valid";
    case PtrState::Null: return "Null";
    case PtrState::Expired: return "Expired";
  }
  return "unknown";
}

const char* to_string(StringState state) {
  switch (state) {
    case StringState::Managed: return "Managed";
    case StringState::View: return "View";
  }
  return "unknown";
}

const char* to_string(BytesState state) {
  switch (state) {
    case BytesState::Managed: return "Managed";
    case BytesState::View: return "View";
  }
  return "unknown";
}

const char* to_string(Visibility vis) {
  switch (vis) {
    case Visibility::Public: return "public";
    case Visibility::Internal: return "internal";
    case Visibility::Private: return "private";
  }
  return "unknown";
}

const char* to_string(Mutability mut) {
  switch (mut) {
    case Mutability::Let: return "let";
    case Mutability::Var: return "var";
  }
  return "unknown";
}

const char* to_string(ReceiverPerm perm) {
  switch (perm) {
    case ReceiverPerm::Const: return "~";
    case ReceiverPerm::Unique: return "~!";
    case ReceiverPerm::Shared: return "~%";
  }
  return "unknown";
}

const char* to_string(RangeKind kind) {
  switch (kind) {
    case RangeKind::To: return "..";
    case RangeKind::ToInclusive: return "..=";
    case RangeKind::Full: return "..";
    case RangeKind::From: return "..";
    case RangeKind::Exclusive: return "..";
    case RangeKind::Inclusive: return "..=";
  }
  return "unknown";
}

const char* to_string(KeyMode mode) {
  switch (mode) {
    case KeyMode::Read: return "read";
    case KeyMode::Write: return "write";
  }
  return "unknown";
}

const char* to_string(KeyBlockMod mod) {
  switch (mod) {
    case KeyBlockMod::Dynamic: return "dynamic";
    case KeyBlockMod::Speculative: return "speculative";
    case KeyBlockMod::Release: return "release";
    case KeyBlockMod::Ordered: return "ordered";
  }
  return "unknown";
}

const char* to_string(RaceHandlerKind kind) {
  switch (kind) {
    case RaceHandlerKind::Return: return "return";
    case RaceHandlerKind::Yield: return "yield";
  }
  return "unknown";
}

const char* to_string(ReduceOp op) {
  switch (op) {
    case ReduceOp::Add: return "+";
    case ReduceOp::Mul: return "*";
    case ReduceOp::Min: return "min";
    case ReduceOp::Max: return "max";
    case ReduceOp::And: return "and";
    case ReduceOp::Or: return "or";
    case ReduceOp::Custom: return "custom";
  }
  return "unknown";
}

const char* to_string(Variance var) {
  switch (var) {
    case Variance::Covariant: return "+";
    case Variance::Contravariant: return "-";
    case Variance::Invariant: return "=";
    case Variance::Bivariant: return "+-";
  }
  return "unknown";
}

const char* to_string(ParamMode mode) {
  switch (mode) {
    case ParamMode::Move: return "move";
  }
  return "unknown";
}

const char* to_string(ForeignContractKind kind) {
  switch (kind) {
    case ForeignContractKind::Assumes: return "@foreign_assumes";
    case ForeignContractKind::Ensures: return "@foreign_ensures";
    case ForeignContractKind::EnsuresError: return "@foreign_ensures(@error)";
    case ForeignContractKind::EnsuresNullResult: return "@foreign_ensures(@null_result)";
  }
  return "unknown";
}

const char* to_string(ContractIntrinsicKind kind) {
  switch (kind) {
    case ContractIntrinsicKind::Result: return "@result";
    case ContractIntrinsicKind::Entry: return "@entry";
  }
  return "unknown";
}

// ===========================================================================
// INTERNAL DUMP HELPERS
// ===========================================================================

namespace {

// Indentation helper
void indent(std::ostream& out, int depth, int indent_size) {
  for (int i = 0; i < depth * indent_size; ++i) {
    out << ' ';
  }
}

// Forward declarations for mutual recursion
void dump_type_impl(std::ostream& out, const Type& type,
                    const DumpOptions& opts, int depth);
void dump_expr_impl(std::ostream& out, const Expr& expr,
                    const DumpOptions& opts, int depth);
void dump_pattern_impl(std::ostream& out, const Pattern& pattern,
                       const DumpOptions& opts, int depth);
void dump_stmt_impl(std::ostream& out, const Stmt& stmt,
                    const DumpOptions& opts, int depth);
void dump_block_impl(std::ostream& out, const Block& block,
                     const DumpOptions& opts, int depth);
void dump_item_impl(std::ostream& out, const ASTItem& item,
                    const DumpOptions& opts, int depth);

// Dump span information
void dump_span(std::ostream& out, const core::Span& span) {
  out << "[" << span.start_line << ":" << span.start_col
      << "-" << span.end_line << ":" << span.end_col << "]";
}

// Dump a path (list of identifiers)
void dump_path(std::ostream& out, const Path& path) {
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (i > 0) out << "::";
    out << path[i];
  }
}

// ===========================================================================
// TYPE DUMPING
// ===========================================================================

void dump_type_impl(std::ostream& out, const Type& type,
                    const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePrim>) {
          out << node.name;
        } else if constexpr (std::is_same_v<T, TypePermType>) {
          out << to_string(node.perm) << " ";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (std::size_t i = 0; i < node.types.size(); ++i) {
            if (i > 0) out << " | ";
            if (node.types[i]) dump_type_impl(out, *node.types[i], opts, depth + 1);
          }
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          out << "(";
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (i > 0) out << ", ";
            if (node.params[i].mode.has_value()) {
              out << to_string(*node.params[i].mode) << " ";
            }
            if (node.params[i].type) {
              dump_type_impl(out, *node.params[i].type, opts, depth + 1);
            }
          }
          out << ") -> ";
          if (node.ret) dump_type_impl(out, *node.ret, opts, depth + 1);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          out << "|";
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (i > 0) out << ", ";
            if (node.params[i].mode.has_value()) {
              out << to_string(*node.params[i].mode) << " ";
            }
            if (node.params[i].type) {
              dump_type_impl(out, *node.params[i].type, opts, depth + 1);
            }
          }
          out << "| -> ";
          if (node.ret) dump_type_impl(out, *node.ret, opts, depth + 1);
          if (node.deps_opt.has_value()) {
            out << " [shared: {";
            for (std::size_t i = 0; i < node.deps_opt->size(); ++i) {
              if (i > 0) out << ", ";
              const auto& dep = (*node.deps_opt)[i];
              out << dep.name << ": ";
              if (dep.type) {
                dump_type_impl(out, *dep.type, opts, depth + 1);
              }
            }
            out << "}]";
          }
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          out << "(";
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (i > 0) out << ", ";
            if (node.elements[i]) dump_type_impl(out, *node.elements[i], opts, depth + 1);
          }
          if (node.elements.size() == 1) out << ";";
          out << ")";
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          out << "[";
          if (node.element) dump_type_impl(out, *node.element, opts, depth + 1);
          out << "; <length>]";
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          out << "[";
          if (node.element) dump_type_impl(out, *node.element, opts, depth + 1);
          out << "]";
        } else if constexpr (std::is_same_v<T, TypeSafePtr>) {
          out << "Ptr<";
          if (node.element) dump_type_impl(out, *node.element, opts, depth + 1);
          out << ">";
          if (node.state.has_value()) out << "@" << to_string(*node.state);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          out << "*" << to_string(node.qual) << " ";
          if (node.element) dump_type_impl(out, *node.element, opts, depth + 1);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          out << "string";
          if (node.state.has_value()) out << "@" << to_string(*node.state);
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          out << "bytes";
          if (node.state.has_value()) out << "@" << to_string(*node.state);
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          out << "$";
          dump_path(out, node.path);
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          dump_path(out, TypeModalRefPath(node.modal_ref));
          const auto& modal_args = TypeModalRefArgs(node.modal_ref);
          if (!modal_args.empty()) {
            out << "<";
            for (std::size_t i = 0; i < modal_args.size(); ++i) {
              if (i > 0) out << ", ";
              if (modal_args[i]) {
                dump_type_impl(out, *modal_args[i], opts, depth + 1);
              }
            }
            out << ">";
          }
          out << "@" << node.state;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          dump_path(out, node.path);
          if (!node.generic_args.empty()) {
            out << "<";
            for (std::size_t i = 0; i < node.generic_args.size(); ++i) {
              if (i > 0) out << ", ";
              if (node.generic_args[i]) {
                dump_type_impl(out, *node.generic_args[i], opts, depth + 1);
              }
            }
            out << ">";
          }
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          dump_path(out, node.path);
          out << "<";
          for (std::size_t i = 0; i < node.args.size(); ++i) {
            if (i > 0) out << ", ";
            if (node.args[i]) {
              dump_type_impl(out, *node.args[i], opts, depth + 1);
            }
          }
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          out << "opaque ";
          dump_path(out, node.path);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << " |: { ... }";
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          out << "Range<";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          out << "RangeInclusive<";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          out << "RangeFrom<";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          out << "RangeTo<";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          out << "RangeToInclusive<";
          if (node.base) dump_type_impl(out, *node.base, opts, depth + 1);
          out << ">";
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          out << "RangeFull";
        } else {
          out << "<unknown type>";
        }
      },
      type.node);

  if (opts.include_spans) {
    out << " ";
    dump_span(out, type.span);
  }
}

// ===========================================================================
// EXPRESSION DUMPING
// ===========================================================================

void dump_expr_impl(std::ostream& out, const Expr& expr,
                    const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  out << node_kind(expr);
  if (opts.include_spans) {
    out << " ";
    dump_span(out, expr.span);
  }
}

// ===========================================================================
// PATTERN DUMPING
// ===========================================================================

void dump_pattern_impl(std::ostream& out, const Pattern& pattern,
                       const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  out << node_kind(pattern);
  if (opts.include_spans) {
    out << " ";
    dump_span(out, pattern.span);
  }
}

// ===========================================================================
// STATEMENT DUMPING
// ===========================================================================

void dump_stmt_impl(std::ostream& out, const Stmt& stmt,
                    const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  out << node_kind(stmt);

  core::Span stmt_span = span_of(stmt);
  if (opts.include_spans) {
    out << " ";
    dump_span(out, stmt_span);
  }
}

void dump_block_impl(std::ostream& out, const Block& block,
                     const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  out << "Block";
  if (opts.include_spans) {
    out << " ";
    dump_span(out, block.span);
  }
  out << " {\n";

  for (const auto& stmt : block.stmts) {
    indent(out, depth + 1, opts.indent_size);
    dump_stmt_impl(out, stmt, opts, depth + 1);
    out << "\n";
  }

  if (block.tail_opt) {
    indent(out, depth + 1, opts.indent_size);
    out << "tail: ";
    dump_expr_impl(out, *block.tail_opt, opts, depth + 1);
    out << "\n";
  }

  indent(out, depth, opts.indent_size);
  out << "}";
}

// ===========================================================================
// ITEM DUMPING
// ===========================================================================

void dump_item_impl(std::ostream& out, const ASTItem& item,
                    const DumpOptions& opts, int depth) {
  if (opts.max_depth >= 0 && depth > opts.max_depth) {
    out << "...";
    return;
  }

  out << node_kind(item);

  core::Span item_span = span_of(item);
  if (opts.include_spans) {
    out << " ";
    dump_span(out, item_span);
  }

  std::visit(
      [&](const auto& decl) {
        using T = std::decay_t<decltype(decl)>;

        if constexpr (std::is_same_v<T, ProcedureDecl>) {
          out << " " << to_string(decl.vis) << " procedure " << decl.name;
        } else if constexpr (std::is_same_v<T, ComptimeProcedureDecl>) {
          out << " " << to_string(decl.vis) << " comptime procedure " << decl.name;
        } else if constexpr (std::is_same_v<T, RecordDecl>) {
          out << " " << to_string(decl.vis) << " record " << decl.name;
        } else if constexpr (std::is_same_v<T, EnumDecl>) {
          out << " " << to_string(decl.vis) << " enum " << decl.name;
        } else if constexpr (std::is_same_v<T, ModalDecl>) {
          out << " " << to_string(decl.vis) << " modal " << decl.name;
        } else if constexpr (std::is_same_v<T, ClassDecl>) {
          out << " " << to_string(decl.vis);
          if (decl.modal) out << " modal";
          out << " class " << decl.name;
        } else if constexpr (std::is_same_v<T, TypeAliasDecl>) {
          out << " " << to_string(decl.vis) << " type " << decl.name;
        } else if constexpr (std::is_same_v<T, DeriveTargetDecl>) {
          out << " derive target " << decl.name;
        } else if constexpr (std::is_same_v<T, StaticDecl>) {
          out << " " << to_string(decl.vis) << " " << to_string(decl.mut);
        } else if constexpr (std::is_same_v<T, ErrorItem>) {
          out << " <error>";
        }
      },
      item);
}

}  // namespace

// ===========================================================================
// PUBLIC DUMP API
// ===========================================================================

void dump(std::ostream& out, const Type& type, const DumpOptions& opts) {
  dump_type_impl(out, type, opts, 0);
}

std::string to_string(const Type& type, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, type, opts);
  return oss.str();
}

void dump(std::ostream& out, const Expr& expr, const DumpOptions& opts) {
  dump_expr_impl(out, expr, opts, 0);
}

std::string to_string(const Expr& expr, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, expr, opts);
  return oss.str();
}

void dump(std::ostream& out, const Pattern& pattern, const DumpOptions& opts) {
  dump_pattern_impl(out, pattern, opts, 0);
}

std::string to_string(const Pattern& pattern, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, pattern, opts);
  return oss.str();
}

void dump(std::ostream& out, const Stmt& stmt, const DumpOptions& opts) {
  dump_stmt_impl(out, stmt, opts, 0);
}

std::string to_string(const Stmt& stmt, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, stmt, opts);
  return oss.str();
}

void dump(std::ostream& out, const Block& block, const DumpOptions& opts) {
  dump_block_impl(out, block, opts, 0);
}

std::string to_string(const Block& block, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, block, opts);
  return oss.str();
}

void dump(std::ostream& out, const ASTItem& item, const DumpOptions& opts) {
  dump_item_impl(out, item, opts, 0);
}

std::string to_string(const ASTItem& item, const DumpOptions& opts) {
  std::ostringstream oss;
  dump(oss, item, opts);
  return oss.str();
}

}  // namespace ultraviolet::ast
