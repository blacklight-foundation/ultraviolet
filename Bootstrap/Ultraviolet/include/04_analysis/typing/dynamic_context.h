#pragma once

#include <span>

#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct DynamicScopeAncestor {
  const ast::AttributeList* attrs = nullptr;
  const core::Span* span = nullptr;
};

inline DynamicScopeAncestor MakeDynamicScopeAncestor(
    const ast::AttributeList& attrs,
    const core::Span& span) {
  return DynamicScopeAncestor{&attrs, &span};
}

const core::Span* FindInnermostDynamic(
    const core::Span& current_span,
    std::span<const DynamicScopeAncestor> ancestors);

bool ComputeDynamicContext(
    const core::Span& current_span,
    std::span<const DynamicScopeAncestor> ancestors);

}  // namespace ultraviolet::analysis
