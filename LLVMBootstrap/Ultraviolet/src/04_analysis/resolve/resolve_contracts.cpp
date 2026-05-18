// =============================================================================
// resolve_contracts.cpp - Contract Resolution
// =============================================================================
//
// SPEC REFERENCE:
//   SPECIFICATION.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   SPECIFICATION.md §8 "Contracts" (Preconditions/Postconditions section)
//
// SOURCE FILE:
//   Migrated from ultraviolet-bootstrap/src/03_analysis/resolve/resolver_items.cpp
//   (Contract resolution embedded in procedure resolution)
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scope_overrides.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsResolveContracts() {
  SPEC_DEF("ResolveContract", "5.1.7");
  SPEC_DEF("ResolvePrecondition", "5.1.7");
  SPEC_DEF("ResolvePostcondition", "5.1.7");
  SPEC_DEF("ResolveEntryCapture", "5.1.7");
  SPEC_DEF("ResolveForeignContract", "5.1.7");
  SPEC_DEF("ResolveInvariantOpt", "5.1.7");
}

template <typename InvariantT>
ResolveResult<std::optional<InvariantT>> ResolveInvariantOptImpl(
    ResolveContext& ctx,
    const std::optional<InvariantT>& invariant_opt,
    bool introduce_self) {
  SpecDefsResolveContracts();
  ResolveResult<std::optional<InvariantT>> result;
  result.ok = true;

  if (!invariant_opt.has_value()) {
    result.value = std::nullopt;
    return result;
  }

  std::optional<ScopedLeadingScope> invariant_scope;
  if (introduce_self) {
    invariant_scope.emplace(*ctx.ctx);
    const auto intro = Intro(*ctx.ctx, "self",
                             Entity{EntityKind::Value, std::nullopt,
                                    std::nullopt, EntitySource::Decl});
    if (!intro.ok) {
      return {false, intro.diag_id, std::nullopt, {}};
    }
  }

  const auto predicate = ResolveExpr(ctx, invariant_opt->predicate);
  if (!predicate.ok) {
    return {false, predicate.diag_id, predicate.span, {},
            predicate.diag_detail, predicate.diag_children};
  }

  InvariantT invariant = *invariant_opt;
  invariant.predicate = predicate.value;
  result.value = std::move(invariant);
  SPEC_RULE("ResolveInvariantOpt-Yes");
  return result;
}

}  // namespace

// -----------------------------------------------------------------------------
// ResolveContract
// -----------------------------------------------------------------------------
// Resolves a contract clause (precondition and postcondition).
// Implements (Resolve-Contract):
//   Gamma |- ResolvePrecondition(pre) => ok /\
//   Gamma |- Intro(`@result`, <Value, T, T, Decl>) => Gamma1 /\
//   Gamma1 |- ResolvePostcondition(post) => ok
//   -> Gamma |- ResolveContract(contract) => ok
//
// The @result binding is introduced for postcondition resolution.

ResolveResult<ast::ContractClause> ResolveContract(
    ResolveContext& ctx,
    const ast::ContractClause& contract) {
  SpecDefsResolveContracts();
  ResolveResult<ast::ContractClause> result;
  result.ok = true;
  result.value = contract;

  // Resolve precondition if present
  if (contract.precondition) {
    const auto pre = ResolveExpr(ctx, contract.precondition);
    if (!pre.ok) {
      SPEC_RULE("ResolvePrecondition-Err");
      return {false, pre.diag_id, pre.span, {}};
    }
    result.value.precondition = pre.value;
    SPEC_RULE("ResolvePrecondition");
  }

  // Resolve postcondition if present
  // Introduce @result binding before resolving postcondition
  if (contract.postcondition) {
    ScopedLeadingScope post_scope(*ctx.ctx);

    // Introduce @result as a value binding
    IntroResult intro = Intro(*ctx.ctx, "@result",
                              Entity{EntityKind::Value, std::nullopt,
                                     std::nullopt, EntitySource::Decl});
    if (!intro.ok) {
      return {false, intro.diag_id, std::nullopt, {}};
    }

    const auto post = ResolveExpr(ctx, contract.postcondition);
    if (!post.ok) {
      SPEC_RULE("ResolvePostcondition-Err");
      return {false, post.diag_id, post.span, {}};
    }
    result.value.postcondition = post.value;
    SPEC_RULE("ResolvePostcondition");
  }

  SPEC_RULE("ResolveContract");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveContractOpt
// -----------------------------------------------------------------------------
// Resolves an optional contract clause.

ResolveResult<std::optional<ast::ContractClause>> ResolveContractOpt(
    ResolveContext& ctx,
    const std::optional<ast::ContractClause>& contract_opt) {
  SpecDefsResolveContracts();
  ResolveResult<std::optional<ast::ContractClause>> result;
  result.ok = true;

  if (!contract_opt.has_value()) {
    result.value = std::nullopt;
    SPEC_RULE("ResolveContractOpt-None");
    return result;
  }

  const auto resolved = ResolveContract(ctx, *contract_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value = resolved.value;
  SPEC_RULE("ResolveContractOpt-Some");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveForeignContract
// -----------------------------------------------------------------------------
// Resolves a foreign contract clause (@foreign_assumes, @foreign_ensures).
// Implements (Resolve-Foreign-Contract):
//   - Resolves each predicate expression
//   - Foreign contracts use special syntax for extern procedures

ResolveResult<ast::ForeignContractClause> ResolveForeignContract(
    ResolveContext& ctx,
    const ast::ForeignContractClause& contract) {
  SpecDefsResolveContracts();
  ResolveResult<ast::ForeignContractClause> result;
  result.ok = true;
  result.value = contract;
  result.value.predicates.clear();
  result.value.predicates.reserve(contract.predicates.size());

  for (const auto& pred : contract.predicates) {
    const auto resolved = ResolveExpr(ctx, pred);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.predicates.push_back(resolved.value);
    SPEC_RULE("ResolveForeignContract-Pred");
  }

  SPEC_RULE("ResolveForeignContract");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveForeignContracts
// -----------------------------------------------------------------------------
// Resolves a list of foreign contract clauses.

ResolveResult<std::vector<ast::ForeignContractClause>> ResolveForeignContracts(
    ResolveContext& ctx,
    const std::vector<ast::ForeignContractClause>& contracts) {
  SpecDefsResolveContracts();
  ResolveResult<std::vector<ast::ForeignContractClause>> result;
  result.ok = true;

  if (contracts.empty()) {
    SPEC_RULE("ResolveForeignContracts-Empty");
    return result;
  }

  result.value.reserve(contracts.size());
  for (const auto& contract : contracts) {
    const auto resolved = ResolveForeignContract(ctx, contract);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveForeignContracts-Cons");
  }

  return result;
}

// -----------------------------------------------------------------------------
// ResolveForeignContractsOpt
// -----------------------------------------------------------------------------
// Resolves an optional list of foreign contract clauses.

ResolveResult<std::optional<std::vector<ast::ForeignContractClause>>>
ResolveForeignContractsOpt(
    ResolveContext& ctx,
    const std::optional<std::vector<ast::ForeignContractClause>>& contracts_opt) {
  SpecDefsResolveContracts();
  ResolveResult<std::optional<std::vector<ast::ForeignContractClause>>> result;
  result.ok = true;

  if (!contracts_opt.has_value()) {
    result.value = std::nullopt;
    SPEC_RULE("ResolveForeignContractsOpt-None");
    return result;
  }

  const auto resolved = ResolveForeignContracts(ctx, *contracts_opt);
  if (!resolved.ok) {
    return {false, resolved.diag_id, resolved.span, {}};
  }

  result.value = resolved.value;
  SPEC_RULE("ResolveForeignContractsOpt-Some");
  return result;
}

// -----------------------------------------------------------------------------
// ResolveEntryCapture
// -----------------------------------------------------------------------------
// Resolves an @entry(expr) intrinsic.
// Implements (Resolve-Entry-Capture):
//   Gamma |- ResolveExpr(expr) => ok /\
//   (Bitcopy(typeof(expr)) \/ Clone(typeof(expr)))
//   -> Gamma |- ResolveEntryCapture(@entry(expr)) => ok
//
// Note: The Bitcopy/Clone check is deferred to type checking.

ResolveResult<ast::EntryExpr> ResolveEntryCapture(
    ResolveContext& ctx,
    const ast::EntryExpr& entry) {
  SpecDefsResolveContracts();
  ResolveResult<ast::EntryExpr> result;
  result.ok = true;
  result.value = entry;

  if (entry.expr) {
    const auto resolved = ResolveExpr(ctx, entry.expr);
    if (!resolved.ok) {
      return {false, resolved.diag_id, resolved.span, {}};
    }
    result.value.expr = resolved.value;
  }

  SPEC_RULE("ResolveEntryCapture");
  return result;
}

ResolveResult<std::optional<ast::TypeInvariant>> ResolveInvariantOpt(
    ResolveContext& ctx,
    const std::optional<ast::TypeInvariant>& invariant_opt) {
  return ResolveInvariantOptImpl(ctx, invariant_opt, /*introduce_self=*/true);
}

ResolveResult<std::optional<ast::LoopInvariant>> ResolveInvariantOpt(
    ResolveContext& ctx,
    const std::optional<ast::LoopInvariant>& invariant_opt) {
  return ResolveInvariantOptImpl(ctx, invariant_opt, /*introduce_self=*/false);
}

}  // namespace ultraviolet::analysis
