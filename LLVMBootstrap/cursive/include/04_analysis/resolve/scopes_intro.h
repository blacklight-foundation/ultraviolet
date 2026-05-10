#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>

#include "04_analysis/typing/context.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

struct IntroResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

IntroResult Intro(ScopeContext& ctx, std::string_view name, const Entity& ent);
IntroResult ShadowIntro(ScopeContext& ctx,
                        std::string_view name,
                        const Entity& ent);

bool InScope(const Scope& scope, std::string_view name);
bool InOuter(const ScopeContext& ctx, std::string_view name);

struct ValidateModuleNamesResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::optional<core::Span> span;
};

ValidateModuleNamesResult ValidateModuleNames(
    const Scope& names,
    const std::unordered_map<IdKey, std::optional<core::Span>>& name_spans);

}  // namespace cursive::analysis
