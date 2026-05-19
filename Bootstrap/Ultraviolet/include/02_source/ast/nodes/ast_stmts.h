// ===========================================================================
// ast_stmts.h - Statement AST Node Definitions
// ===========================================================================
//
// PURPOSE:
//   Statement AST node definitions. Contains all struct definitions for
//   statement nodes in the Ultraviolet grammar, plus the Stmt variant and
//   Block structure.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.3.2.6
//
// ===========================================================================

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast_common.h"
#include "02_source/ast/nodes/ast_attributes.h"
#include "02_source/ast/nodes/ast_patterns.h"

namespace ultraviolet::ast
{

  // ===========================================================================
  // Binding Support
  // ===========================================================================
  // Binding captures the common structure of let and var declarations,
  // including pattern, optional type annotation, binding operator (= or :=),
  // and initializer expression.

  struct Binding
  {
    AttributeList attrs;
    PatternPtr pat;
    TypePtr type_opt;
    Token op; // = (movable) or := (immovable)
    ExprPtr init;
    ultraviolet::core::Span span;
  };

  inline TypePtr BindingAnnotationTypeOpt(const Binding& binding)
  {
    if (binding.type_opt) {
      return binding.type_opt;
    }
    if (!binding.pat) {
      return nullptr;
    }
    if (const auto* typed = std::get_if<TypedPattern>(&binding.pat->node)) {
      return typed->type;
    }
    return nullptr;
  }

  // ===========================================================================
  // Binding Statements
  // ===========================================================================
  // Let and var bindings create new named values.

  /// LetStmt: let binding (let x: T = v)
  /// Creates an immutable binding. Uses Binding for common structure.
  struct LetStmt
  {
    Binding binding;
    ultraviolet::core::Span span;
  };

  /// VarStmt: var binding (var x: T = v)
  /// Creates a mutable binding. Uses Binding for common structure.
  struct VarStmt
  {
    Binding binding;
    ultraviolet::core::Span span;
  };

  /// UsingLocalStmt: local alias (using source as alias)
  /// Compile-time rename. Binds `alias` to the same Entity that `source`
  /// resolves to. No storage is allocated; no runtime effect.
  /// Spec: §7.2 UsingAlias, §18.3.
  struct UsingLocalStmt
  {
    Identifier source;
    std::optional<SpliceIdentNode> source_splice_opt;
    Identifier alias;
    std::optional<SpliceIdentNode> alias_splice_opt;
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Assignment Statements
  // ===========================================================================

  /// AssignStmt: simple assignment (x = v)
  /// Assigns a value to a place expression. The place must be mutable.
  struct AssignStmt
  {
    ExprPtr place;
    ExprPtr value;
    ultraviolet::core::Span span;
  };

  /// CompoundAssignStmt: compound assignment (x += v, x -= v, etc.)
  /// Combines a binary operation with assignment.
  /// op contains the compound operator identifier (+=, -=, *=, /=, %=,
  /// &=, |:, ^=, <<=, >>=).
  struct CompoundAssignStmt
  {
    ExprPtr place;
    Identifier op; // +=, -=, *=, /=, %=, &=, |:, ^=, <<=, >>=
    ExprPtr value;
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Expression Statement
  // ===========================================================================

  /// ExprStmt: expression statement (expr;)
  /// Evaluates an expression for its side effects. The result is discarded.
  struct ExprStmt
  {
    ExprPtr value;
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Resource Statements
  // ===========================================================================
  // These statements manage resources and memory regions.

  /// DeferStmt: defer statement (defer { })
  /// Defers execution of the body until the enclosing scope exits.
  /// Useful for cleanup operations.
  struct DeferStmt
  {
    BlockPtr body;
    ultraviolet::core::Span span;
  };

  /// RegionStmt: region statement (region { }, region (opts) { }, region as r { })
  /// Creates a memory region for arena allocation.
  /// - opts_opt: Optional RegionOptions expression
  /// - alias_opt: Optional identifier to bind the region
  struct RegionStmt
  {
    ExprPtr opts_opt;
    std::optional<Identifier> alias_opt;
    std::optional<SpliceIdentNode> alias_splice_opt;
    BlockPtr body;
    ultraviolet::core::Span span;
  };

  /// FrameStmt: frame statement (frame { }, frame target { })
  /// Creates a sub-frame within an existing region.
  /// - target_opt: Optional identifier naming the target region
  struct FrameStmt
  {
    std::optional<Identifier> target_opt;
    BlockPtr body;
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Control Statements
  // ===========================================================================

  /// ReturnStmt: return statement (return, return v)
  /// Returns from the current procedure.
  /// - value_opt: Optional return value (unit if absent)
  struct ReturnStmt
  {
    ExprPtr value_opt;
    ultraviolet::core::Span span;
  };

  /// BreakStmt: break statement (break, break v)
  /// Exits the enclosing loop.
  /// - value_opt: Optional value (for loop expressions that yield values)
  struct BreakStmt
  {
    ExprPtr value_opt;
    ultraviolet::core::Span span;
  };

  /// ContinueStmt: continue statement (continue)
  /// Skips to the next iteration of the enclosing loop.
  struct ContinueStmt
  {
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Unsafe and Special Statements
  // ===========================================================================

  /// UnsafeBlockStmt: unsafe block (unsafe { })
  /// Contains code that may perform unsafe operations.
  /// Required for raw pointer operations, FFI calls, transmute, etc.
  struct UnsafeBlockStmt
  {
    BlockPtr body;
    ultraviolet::core::Span span;
  };

  /// CtStmt: compile-time statement block (comptime { ... })
  /// Executes only during Phase 2 expansion and does not survive into runtime
  /// statement lowering.
  struct CtStmt
  {
    AttributeList attrs;
    BlockPtr body;
    ultraviolet::core::Span span;
  };
  using ComptimeStmt = CtStmt;

  // ===========================================================================
  // Key Block Statement
  // ===========================================================================
  // Key blocks provide synchronized access to shared data using the key system.
  // Syntax: %read path { }, %write path [ordered] { },
  // %release read path { }, or %speculative write path { }.

  /// KeyBlockStmt: key block (%read path { })
  /// Acquires keys for synchronized access to shared data.
  /// - kind: Source head form.
  /// - paths: Key paths identifying the data to access
  /// - mode: Effective access mode (Read or Write)
  /// - options: Options that apply to the whole path set
  /// - body: Block executed while holding the keys
  ///
  /// Note: KeyMode, KeyBlockKind, and KeyBlockOptions are defined in ast_common.h
  ///       KeyPathExpr is defined in ast_common.h
  struct KeyBlockStmt
  {
    AttributeList attrs;
    KeyBlockKind kind = KeyBlockKind::Read;
    std::vector<KeyPathExpr> paths;
    KeyMode mode = KeyMode::Read;
    KeyBlockOptions options;
    BlockPtr body;
    ultraviolet::core::Span span;
  };

  /// ErrorStmt: parse-error sentinel node
  /// Represents a statement that failed to parse. Allows the parser
  /// to continue after errors for better error recovery.
  struct ErrorStmt
  {
    ultraviolet::core::Span span;
  };

  // ===========================================================================
  // Stmt Variant
  // ===========================================================================
  // Stmt is a variant type encompassing all statement kinds.

  using Stmt = std::variant<
      // Binding statements
      LetStmt,
      VarStmt,
      UsingLocalStmt,
      // Assignment statements
      AssignStmt,
      CompoundAssignStmt,
      // Expression statement
      ExprStmt,
      // Resource statements
      DeferStmt,
      RegionStmt,
      FrameStmt,
      // Control statements
      ReturnStmt,
      BreakStmt,
      ContinueStmt,
      // Unsafe and special statements
      UnsafeBlockStmt,
      CtStmt,
      KeyBlockStmt,
      ErrorStmt>;

  // ===========================================================================
  // Block Structure
  // ===========================================================================
  // Block represents a sequence of statements with an optional trailing
  // expression (tail expression). The tail expression becomes the value
  // of the block when used as an expression.

  struct Block
  {
    std::vector<Stmt> stmts;
    ExprPtr tail_opt;
    ultraviolet::core::Span span;
  };

} // namespace ultraviolet::ast
