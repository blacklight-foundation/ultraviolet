// =============================================================================
// runtime_scope.cpp - Runtime Scope Stack Operations
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §4179-4242 (Runtime Scope Data Model)
//
// =============================================================================

#include "04_analysis/runtime_eval/runtime_scope.h"

#include "00_core/assert_spec.h"

namespace cursive::analysis::runtime_eval {

namespace {

static inline void SpecDefsRuntimeScope() {
  SPEC_DEF("ScopeEntry", "4.6.1");
  SPEC_DEF("ScopeId", "4.6.1");
  SPEC_DEF("ScopeNames", "4.6.1");
  SPEC_DEF("ScopeVals", "4.6.1");
  SPEC_DEF("NearestScope", "4.6.1");
  SPEC_DEF("LookupBind", "4.6.1");
  SPEC_DEF("BindingValue", "4.6.1");
  SPEC_DEF("BindInitScope", "4.5.3");
}

}  // namespace

// §4179: ScopeId(⟨sid, ...⟩) = sid
ScopeId ScopeIdOf(const ScopeEntry& entry) {
  SpecDefsRuntimeScope();
  return entry.sid;
}

// §4181: ScopeNames(⟨_, _, names, ...⟩) = names
const std::unordered_map<std::string, std::vector<BindId>>&
ScopeNamesOf(const ScopeEntry& entry) {
  SpecDefsRuntimeScope();
  return entry.names;
}

// §4225-4228:
//   NearestScope([], x) = ⊥
//   NearestScope(scope :: ss, x) =
//     scope                  if ScopeNames(scope)[x] defined
//     NearestScope(ss, x)    otherwise
std::optional<const ScopeEntry*> NearestScope(const ScopeStack& stack,
                                              std::string_view name) {
  SpecDefsRuntimeScope();
  const std::string key(name);
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    const auto& names = ScopeNamesOf(*it);
    if (names.find(key) != names.end()) {
      SPEC_RULE("NearestScope-Found");
      return &(*it);
    }
  }
  SPEC_RULE("NearestScope-None");
  return std::nullopt;
}

// §4222-4223: Last([a]) = a; Last(a :: as) = Last(as)
static BindId LastBindId(const std::vector<BindId>& ids) {
  return ids.back();
}

// §4230:
//   LookupBind(σ, x) = ⟨ScopeId(scope), b, x⟩ ⇔
//     NearestScope(ScopeStack(σ), x) = scope ∧
//     b = Last(ScopeNames(scope)[x])
std::optional<BindingRef> LookupBind(const ScopeStack& stack,
                                     std::string_view name) {
  SpecDefsRuntimeScope();
  const auto scope = NearestScope(stack, name);
  if (!scope.has_value()) {
    return std::nullopt;
  }
  const std::string key(name);
  const auto& names = ScopeNamesOf(**scope);
  const auto it = names.find(key);
  if (it == names.end() || it->second.empty()) {
    return std::nullopt;
  }
  SPEC_RULE("LookupBind");
  return BindingRef{ScopeIdOf(**scope), LastBindId(it->second), key};
}

// §4231:
//   BindingValue(σ, ⟨sid, bind_id, x⟩) = v ⇔
//     ScopeById(ScopeStack(σ), sid) = scope ∧
//     ScopeVals(scope)[bind_id] = v
std::optional<BindingValue> BindingValueOf(const ScopeStack& stack,
                                           const BindingRef& ref) {
  SpecDefsRuntimeScope();
  for (const auto& entry : stack) {
    if (entry.sid != ref.scope_id) {
      continue;
    }
    const auto it = entry.vals.find(ref.bind_id);
    if (it == entry.vals.end()) {
      return std::nullopt;
    }
    SPEC_RULE("BindingValue");
    return it->second;
  }
  return std::nullopt;
}

// §3818-3822:
//   BindInitScope(e) = BindScope(s) ⇔
//     (s = LetStmt(binding) ∧ InitExpr(binding) = e) ∨
//     (s = VarStmt(binding) ∧ InitExpr(binding) = e) ∨
//     (s = ShadowLetStmt(_, _, e)) ∨
//     (s = ShadowVarStmt(_, _, e))
std::optional<const ast::Stmt*> BindInitScope(
    const std::vector<ast::Stmt>& stmts,
    const ast::Expr* init_expr) {
  SpecDefsRuntimeScope();
  if (!init_expr) {
    return std::nullopt;
  }
  for (const auto& stmt : stmts) {
    const bool matches = std::visit(
        [&](const auto& node) -> bool {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ast::LetStmt> ||
                        std::is_same_v<T, ast::VarStmt>) {
            return node.binding.init.get() == init_expr;
          } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
            // UsingLocalStmt is a compile-time alias; no runtime expression.
            (void)node;
            return false;
          } else {
            return false;
          }
        },
        stmt);
    if (matches) {
      SPEC_RULE("BindInitScope");
      return &stmt;
    }
  }
  return std::nullopt;
}

}  // namespace cursive::analysis::runtime_eval
