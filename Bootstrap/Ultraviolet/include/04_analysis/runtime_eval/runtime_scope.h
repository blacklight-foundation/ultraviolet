#pragma once

// =============================================================================
// runtime_scope.h - Runtime Scope Stack Operations
// =============================================================================
//
// SPEC REFERENCE:
//   Docs/SPECIFICATION.md §4179-4242 (Runtime Scope Data Model)
//
// Defines the runtime scope stack data model and the core lookup
// operations: NearestScope, LookupBind, LookupVal, BindInitScope.
//
// These operations model how the runtime evaluator resolves names and
// values through the scope stack during program execution.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "02_source/ast/ast.h"

namespace ultraviolet::analysis::runtime_eval {

using ScopeId = std::uint64_t;
using BindId = std::uint64_t;
using Addr = std::uint64_t;

// §4218: BindingValue = Value ∪ {Alias(addr) | addr ∈ Addr}
struct AliasValue {
  Addr addr;
};

using BindingValue = std::variant<std::monostate, AliasValue>;

// §4179-4185: ScopeEntry = ⟨sid, cleanup, names, vals, states⟩
struct ScopeEntry {
  ScopeId sid;
  std::vector<ast::Stmt> cleanup;
  std::unordered_map<std::string, std::vector<BindId>> names;
  std::unordered_map<BindId, BindingValue> vals;
  std::unordered_map<std::string, std::string> states;
};

// §4185: ScopeStack(σ) ∈ [ScopeEntry]
using ScopeStack = std::vector<ScopeEntry>;

// §4230: LookupBind result: ⟨ScopeId, BindId, name⟩
struct BindingRef {
  ScopeId scope_id;
  BindId bind_id;
  std::string name;
};

// §4179: ScopeId(⟨sid, ...⟩) = sid
ScopeId ScopeIdOf(const ScopeEntry& entry);

// §4181: ScopeNames(⟨_, _, names, ...⟩) = names
const std::unordered_map<std::string, std::vector<BindId>>&
ScopeNamesOf(const ScopeEntry& entry);

// §4225-4228: NearestScope
std::optional<const ScopeEntry*> NearestScope(const ScopeStack& stack,
                                              std::string_view name);

// §4230: LookupBind(σ, x)
std::optional<BindingRef> LookupBind(const ScopeStack& stack,
                                     std::string_view name);

// §4231: BindingValue(σ, ref)
std::optional<BindingValue> BindingValueOf(const ScopeStack& stack,
                                           const BindingRef& ref);

// §3818-3822: BindInitScope(e) — matches the enclosing binding statement
std::optional<const ast::Stmt*> BindInitScope(
    const std::vector<ast::Stmt>& stmts,
    const ast::Expr* init_expr);

}  // namespace ultraviolet::analysis::runtime_eval
