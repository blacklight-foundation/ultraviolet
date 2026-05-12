#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "04_analysis/typing/types.h"
#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

using ParamSubst = std::unordered_map<std::string, std::string>;

struct ScopeContext;

// C0X Extension: Contract System - Contract Type Checking

// Contract checking result
struct ContractCheckResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

// Contract context
struct ContractContext {
  // Available bindings
  std::map<std::string, TypeRef> params;
  std::unordered_set<std::string> moved_params;
  std::unordered_set<std::string> local_bindings;
  TypeRef receiver_type;
  TypeRef return_type;
  const ScopeContext* scope_ctx = nullptr;
  
  // Context state
  bool is_postcondition = false;
  bool in_type_invariant = false;
  bool in_loop_invariant = false;
  bool allow_responsibility_moves = false;
};

// Check well-formedness of contract clause
// WF-Contract: pure predicates, correct typing
ContractCheckResult CheckContractWellFormed(
    const ContractContext& ctx,
    const ast::ContractClause& contract);

// Check precondition expression
// Γ_pre: parameters, receiver, enclosing scope (no @result, @entry)
ContractCheckResult CheckPrecondition(
    const ContractContext& ctx,
    const ast::ExprPtr& expr);

// Check postcondition expression
// Γ_post: add @result, @entry(), mutable params at post-state
ContractCheckResult CheckPostcondition(
    const ContractContext& ctx,
    const ast::ExprPtr& expr);

// Check purity of expression
// Pure: no capability params, no observable mutation
ContractCheckResult CheckPurity(const ast::ExprPtr& expr);
ContractCheckResult CheckPurity(const ContractContext& ctx,
                                const ast::ExprPtr& expr);

// Check type invariant
struct TypeInvariantResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool has_public_mutable_fields = false;
};

TypeInvariantResult CheckTypeInvariant(
    const ContractContext& ctx,
    const ast::TypeInvariant& invariant);

// Check loop invariant
ContractCheckResult CheckLoopInvariant(
    const ContractContext& ctx,
    const ast::LoopInvariant& invariant);

// Behavioral subtyping check (LSP)
// impl pre >= class pre, impl post <= class post
struct BehavioralSubtypingResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  bool precondition_weaker = true;   // impl pre is weaker/equal
  bool postcondition_stronger = true; // impl post is stronger/equal
};

BehavioralSubtypingResult CheckBehavioralSubtyping(
    const ast::ContractClause& class_contract,
    const ast::ContractClause& impl_contract);

// ============================================================================
// Contract Purity Checking (contract_purity.cpp)
// ============================================================================

// Check full purity of contract expression
// Returns error if expression contains impure operations
ContractCheckResult CheckContractPurity(const ast::ExprPtr& expr);

// Check if expression is a capability call
bool IsCapabilityCall(const ast::ExprPtr& expr);

// Check if expression has observable side effects
bool HasSideEffects(const ast::ExprPtr& expr);

// Check if expression is pure (no side effects, no capability calls)
bool IsPureExpression(const ast::ExprPtr& expr);

// Check if an operator is pure for the given arity
bool IsPureOperator(std::string_view op, bool is_binary);

// Check if expression is mutation-free
bool CheckMutationFree(const ast::ExprPtr& expr);

// ============================================================================
// Contract Intrinsics (contract_intrinsics.cpp)
// ============================================================================

// Intrinsic kinds
enum class IntrinsicKind {
  None,
  Result,  // @result
  Entry    // @entry(expr)
};

// Entry expression occurrence info
struct EntryOccurrence {
  ast::ExprPtr captured_expr;
  core::Span span;
};

// Intrinsic validation result
struct IntrinsicValidationResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  std::vector<core::Span> result_occurrences;
  std::vector<EntryOccurrence> entry_occurrences;
};

// Validate @result intrinsic usage
IntrinsicValidationResult ValidateResultIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx);

// Validate @entry intrinsic usage
IntrinsicValidationResult ValidateEntryIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx);

// Validate that a type is suitable for @entry (Bitcopy)
bool ValidateEntryType(const TypeRef& type);

// Foreign contract assumption
struct ForeignAssumption {
  ast::ExprPtr predicate;
  core::Span span;
};

// Foreign contract guarantee
struct ForeignGuarantee {
  ast::ExprPtr predicate;
  core::Span span;
  bool is_error_condition = false;
  bool is_null_condition = false;
};

// Foreign contract resolution result
struct ForeignContractResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<ForeignAssumption> assumptions;
  std::vector<ForeignGuarantee> guarantees;
  bool has_error_clause = false;
  bool has_null_clause = false;
};

// Resolve @foreign_assumes predicates
ForeignContractResult ResolveForeignAssumes(
    const ast::ForeignContractClause& clause);

// Resolve @foreign_ensures predicates
ForeignContractResult ResolveForeignEnsures(
    const ast::ForeignContractClause& clause,
    const TypeRef& return_type);

// Handle @foreign_ensures(@error: pred)
ForeignContractResult HandleForeignError(
    const ast::ForeignContractClause& clause);

// Handle @foreign_ensures(@null_result: pred)
ForeignContractResult HandleForeignNull(
    const ast::ForeignContractClause& clause);

// Intrinsic evaluation result
struct IntrinsicEvalResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
  IntrinsicKind kind = IntrinsicKind::None;
  TypeRef resolved_type;
  ast::ExprPtr captured_expr;  // For @entry
};

// Evaluate contract intrinsic expression
IntrinsicEvalResult EvaluateContractIntrinsic(
    const ast::ExprPtr& expr,
    const ContractContext& ctx);

// Check if expression contains contract intrinsics (@result, @entry)
bool ContainsContractIntrinsics(const ast::ExprPtr& expr);

// Validate all intrinsics in a contract clause
ContractCheckResult ValidateContractIntrinsics(
    const ast::ContractClause& contract,
    const ContractContext& ctx);

}  // namespace cursive::analysis
