#include "04_analysis/resolve/scopes_intro.h"

#include <algorithm>
#include <vector>

#include "00_core/assert_spec.h"
#include "01_project/language_profile.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsIntro() {
  SPEC_DEF("dom", "5.1.2");
  SPEC_DEF("Scopes", "5.1.2");
  SPEC_DEF("InScope", "5.1.2");
  SPEC_DEF("InOuter", "5.1.2");
  SPEC_DEF("Names", "5.1.2");
}

bool ContainsKey(const std::vector<IdKey>& keys, const IdKey& key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool IsUniverseProtectedKey(const IdKey& key) {
  return ContainsKey(PrimTypeKeys(), key) ||
         ContainsKey(SpecialTypeKeys(), key) ||
         ContainsKey(AsyncTypeKeys(), key);
}

bool IsModuleScopeCurrent(const ScopeList& scopes) {
  if (scopes.size() < 2) {
    return false;
  }
  return &scopes.front() == &ModuleScope(scopes);
}

bool ReservedLanguageRoot(std::string_view name) {
  return IdEq(name, project::ActiveLanguageProfile().runtime_root);
}

std::optional<core::Span> SpanForKey(
    const std::unordered_map<IdKey, std::optional<core::Span>>& name_spans,
    const IdKey& key) {
  const auto it = name_spans.find(key);
  if (it == name_spans.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace

bool InScope(const Scope& scope, std::string_view name) {
  SpecDefsIntro();
  return scope.find(IdKeyOf(name)) != scope.end();
}

bool InOuter(const ScopeContext& ctx, std::string_view name) {
  SpecDefsIntro();
  const auto& scopes = Scopes(ctx);
  if (scopes.empty()) {
    return false;
  }
  for (std::size_t i = 1; i < scopes.size(); ++i) {
    if (InScope(scopes[i], name)) {
      return true;
    }
  }
  return false;
}

IntroResult Intro(ScopeContext& ctx, std::string_view name, const Entity& ent) {
  SpecDefsIntro();
  if (ReservedGen(name) || ReservedLanguageRoot(name)) {
    SPEC_RULE("Intro-Reserved-Id-Err");
    return {false, "Intro-Reserved-Id-Err"};
  }

  auto& scopes = Scopes(ctx);
  if (scopes.empty()) {
    return {false, "ResolveExpr-Ident-Err"};
  }

  if (InScope(scopes.front(), name)) {
    SPEC_RULE("Intro-Dup");
    return {false, "Intro-Dup"};
  }
  if (InOuter(ctx, name)) {
    SPEC_RULE("Intro-Outer-Err");
    return {false, "Intro-Outer-Err"};
  }

  const auto key = IdKeyOf(name);
  if (IsModuleScopeCurrent(scopes) && IsUniverseProtectedKey(key)) {
    if (ContainsKey(PrimTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Prim-Shadow-Err");
      return {false, "Validate-Module-Prim-Shadow-Err"};
    }
    if (ContainsKey(SpecialTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Special-Shadow-Err");
      return {false, "Validate-Module-Special-Shadow-Err"};
    }
    if (ContainsKey(AsyncTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Async-Shadow-Err");
      return {false, "Validate-Module-Async-Shadow-Err"};
    }
    return {false, "ResolveExpr-Ident-Err"};
  }

  SPEC_RULE("Intro-Ok");
  scopes.front().emplace(key, ent);
  return {true, std::nullopt};
}

IntroResult ShadowIntro(ScopeContext& ctx,
                        std::string_view name,
                        const Entity& ent) {
  SpecDefsIntro();
  if (ReservedGen(name) || ReservedLanguageRoot(name)) {
    SPEC_RULE("Shadow-Reserved-Id-Err");
    return {false, "Shadow-Reserved-Id-Err"};
  }

  auto& scopes = Scopes(ctx);
  if (scopes.empty()) {
    return {false, "ResolveExpr-Ident-Err"};
  }

  if (InScope(scopes.front(), name)) {
    SPEC_RULE("Intro-Dup");
    return {false, "Intro-Dup"};
  }
  if (!InOuter(ctx, name)) {
    SPEC_RULE("Shadow-Unnecessary");
    return {false, "Shadow-Unnecessary"};
  }

  const auto key = IdKeyOf(name);
  if (IsModuleScopeCurrent(scopes) && IsUniverseProtectedKey(key)) {
    if (ContainsKey(PrimTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Prim-Shadow-Err");
      return {false, "Validate-Module-Prim-Shadow-Err"};
    }
    if (ContainsKey(SpecialTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Special-Shadow-Err");
      return {false, "Validate-Module-Special-Shadow-Err"};
    }
    if (ContainsKey(AsyncTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Async-Shadow-Err");
      return {false, "Validate-Module-Async-Shadow-Err"};
    }
    return {false, "ResolveExpr-Ident-Err"};
  }

  SPEC_RULE("Shadow-Ok");
  scopes.front().emplace(key, ent);
  return {true, std::nullopt};
}

ValidateModuleNamesResult ValidateModuleNames(
    const Scope& names,
    const std::unordered_map<IdKey, std::optional<core::Span>>& name_spans) {
  SpecDefsIntro();
  std::vector<IdKey> keys;
  keys.reserve(names.size());
  for (const auto& [key, ent] : names) {
    (void)ent;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());

  for (const auto& key : keys) {
    if (ReservedGen(key) || ReservedLanguageRoot(key)) {
      SPEC_RULE("Intro-Reserved-Id-Err");
      return {false, "Intro-Reserved-Id-Err", SpanForKey(name_spans, key)};
    }
  }
  for (const auto& key : keys) {
    if (KeywordKey(key)) {
      SPEC_RULE("Validate-Module-Keyword-Err");
      return {false, "Validate-Module-Keyword-Err", SpanForKey(name_spans, key)};
    }
  }
  for (const auto& key : keys) {
    if (ContainsKey(PrimTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Prim-Shadow-Err");
      return {false, "Validate-Module-Prim-Shadow-Err", SpanForKey(name_spans, key)};
    }
  }
  for (const auto& key : keys) {
    if (ContainsKey(SpecialTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Special-Shadow-Err");
      return {false, "Validate-Module-Special-Shadow-Err", SpanForKey(name_spans, key)};
    }
  }
  for (const auto& key : keys) {
    if (ContainsKey(AsyncTypeKeys(), key)) {
      SPEC_RULE("Validate-Module-Async-Shadow-Err");
      return {false, "Validate-Module-Async-Shadow-Err", SpanForKey(name_spans, key)};
    }
  }

  SPEC_RULE("Validate-Module-Ok");
  return {true, std::nullopt, std::nullopt};
}

}  // namespace ultraviolet::analysis
