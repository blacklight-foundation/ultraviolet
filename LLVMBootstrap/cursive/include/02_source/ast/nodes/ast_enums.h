// ===========================================================================
// ast_enums.h - AST Enumeration Types
// ===========================================================================
//
// PURPOSE:
//   Enumeration types used across AST node categories. Centralizing enums
//   prevents circular dependencies between category headers while keeping
//   related values together.
//
// ---------------------------------------------------------------------------
// SPEC REFERENCE: docs/CursiveSpecification.md
// ---------------------------------------------------------------------------
//   Section 3.3.2.3 - Type Nodes (Lines 2875-2898)
//     - Perm = const | unique | shared (2886)
//     - Qual = imm | mut (2887)
//     - PtrStateOpt, StringStateOpt, BytesStateOpt (2888-2890)
//     - ParamMode (2893)
//
//   Section 3.3.2.4 - Expression Nodes
//     - RangeKind (exclusive, inclusive, from, to, full)
//     - RaceHandlerKind (return, yield)
//     - ReduceOp (add, mul, min, max, and, or, custom)
//
//   Section 3.3.2.6 - Statement Nodes
//     - KeyMode (read, write)
//     - KeyBlockMod (dynamic, speculative, release)
//
//   Section 3.3.2.5 - Declaration Nodes (Lines 2680-2873)
//     - Visibility (public, internal, private, protected)
//     - Mutability (let, var)
//     - ReceiverPerm (const, unique, shared)
//     - Variance (covariant, contravariant, invariant, bivariant)
//
// ===========================================================================

#pragma once

namespace cursive::ast {

// ===========================================================================
// Permission and Qualifier Enums
// ===========================================================================

/// Parameter passing mode for function parameters.
/// Currently only Move is supported, indicating ownership transfer.
enum class ParamMode {
  Move,
};

/// Type permission qualifiers controlling access and aliasing.
/// Forms a lattice: unique <: shared <: const
enum class TypePerm {
  Const,   // Read-only access, unlimited aliasing
  Unique,  // Exclusive mutable access, no aliases
  Shared,  // Synchronized shared access (requires key system)
};

/// Raw pointer qualifiers for FFI interop.
enum class RawPtrQual {
  Imm,  // Immutable raw pointer (*imm T)
  Mut,  // Mutable raw pointer (*mut T)
};

/// Method receiver permission shorthands.
/// - Const  (~)  expands to const Self, read-only
/// - Unique (~!) expands to unique Self, mutable
/// - Shared (~%) expands to shared Self, synchronized
enum class ReceiverPerm {
  Const,
  Unique,
  Shared,
};

// ===========================================================================
// State Enums
// ===========================================================================

/// Safe pointer state tracking for Ptr<T>.
enum class PtrState {
  Valid,    // Points to valid memory
  Null,     // Null pointer
  Expired,  // Points to deallocated memory (internal state)
};

/// String type state tracking for string@State.
enum class StringState {
  Managed,  // Heap-allocated, owned string
  View,     // Borrowed string view
};

/// Bytes type state tracking for bytes@State.
enum class BytesState {
  Managed,  // Heap-allocated, owned bytes
  View,     // Borrowed bytes view
};

// ===========================================================================
// Mode and Mutability Enums
// ===========================================================================

/// Key access mode for synchronized data access.
enum class KeyMode {
  Read,   // Read-only access
  Write,  // Write access (default)
};

/// Binding mutability for let/var declarations.
enum class Mutability {
  Let,  // Immutable binding
  Var,  // Mutable binding
};

// ===========================================================================
// Visibility and Variance Enums
// ===========================================================================

/// Declaration visibility modifiers.
enum class Visibility {
  Public,     // Visible everywhere
  Internal,   // Visible within the assembly (default)
  Private,    // Visible only in declaring module
};

/// Type parameter variance for generics.
enum class Variance {
  Covariant,      // + (output position)
  Contravariant,  // - (input position)
  Invariant,      // Neither + nor -
  Bivariant,      // Both + and - (unused)
};

// ===========================================================================
// Expression-Specific Kind Enums
// ===========================================================================

/// Range expression kinds for loop iteration and slicing.
/// Determines which bounds are included/excluded.
enum class RangeKind {
  To,           // ..end (exclusive end, open start)
  ToInclusive,  // ..=end (inclusive end, open start)
  Full,         // .. (unbounded)
  From,         // start.. (open end)
  Exclusive,    // start..end (exclusive end)
  Inclusive,    // start..=end (inclusive end)
};

/// Race expression handler kinds.
enum class RaceHandlerKind {
  Return,  // Handler returns a value
  Yield,   // Handler yields a value
};

/// Fence ordering for fence expressions.
enum class FenceOrder {
  Acquire,  // fence(acquire)
  Release,  // fence(release)
  SeqCst,   // fence(seqcst)
};

/// Quote form discriminator for compile-time quote expressions.
enum class QuoteKind {
  Unspecified,
  Expr,
  Stmt,
  Item,
  Type,
  Pattern,
};

/// Reduction operators for parallel dispatch.
enum class ReduceOp {
  Add,     // + reduction
  Mul,     // * reduction
  Min,     // min reduction
  Max,     // max reduction
  And,     // and reduction
  Or,      // or reduction
  Custom,  // Custom identifier-based reduction
};

// ===========================================================================
// Concurrency Option Kind Enums
// ===========================================================================

/// Parallel block option kinds.
enum class ParallelOptionKind {
  Cancel,  // cancel: CancelToken
  Name,    // name: string literal
  Workgroup,   // workgroup: dim3 const
  Workgroups,  // workgroups: dim3 const
};

/// Spawn expression option kinds.
enum class SpawnOptionKind {
  Name,         // name: string literal
  Affinity,     // affinity: CpuSet expression
  Priority,     // priority: Priority expression
};

/// Dispatch expression option kinds.
enum class DispatchOptionKind {
  Reduce,   // reduce: operator
  Ordered,  // ordered flag
  Chunk,    // chunk: usize expression
  Workgroup,  // workgroup: dim3 const
};

// ===========================================================================
// Key System Modifier Enums
// ===========================================================================

/// Key block modifiers for synchronized access.
enum class KeyBlockMod {
  Dynamic,      // Runtime key acquisition
  Speculative,  // Speculative execution (may roll back)
  Release,      // Release held keys after block completes
  Ordered,      // Canonical ordered acquisition/commit behavior
};

// ===========================================================================
// Contract Kind Enums
// ===========================================================================

/// Foreign contract clause kinds for extern blocks.
enum class ForeignContractKind {
  Assumes,          // @foreign_assumes - preconditions foreign code expects
  Ensures,          // @foreign_ensures - postconditions on success
  EnsuresError,     // @foreign_ensures(@error: ...) - postconditions on error
  EnsuresNullResult,  // @foreign_ensures(@null_result: ...) - postconditions on null
};

/// Contract intrinsic expression kinds.
enum class ContractIntrinsicKind {
  Result,  // @result - the return value in postconditions
  Entry,   // @entry(expr) - value at procedure entry
};

}  // namespace cursive::ast
