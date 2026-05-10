#include "04_analysis/typing/dynamic_context.h"

#include <cstddef>

#include "02_source/attributes/attribute_registry.h"

namespace cursive::analysis {

namespace {

bool SpanContains(const core::Span& outer, const core::Span& inner) {
  return outer.file == inner.file &&
         outer.start_offset <= inner.start_offset &&
         inner.end_offset <= outer.end_offset;
}

std::size_t SpanWidth(const core::Span& span) {
  return span.end_offset >= span.start_offset
             ? span.end_offset - span.start_offset
             : 0;
}

}  // namespace

const core::Span* FindInnermostDynamic(
    const core::Span& current_span,
    std::span<const DynamicScopeAncestor> ancestors) {
  const core::Span* innermost = nullptr;
  for (const DynamicScopeAncestor& ancestor : ancestors) {
    if (ancestor.attrs == nullptr || ancestor.span == nullptr) {
      continue;
    }
    if (!HasAttribute(*ancestor.attrs, attrs::kDynamic) ||
        !SpanContains(*ancestor.span, current_span)) {
      continue;
    }
    if (innermost == nullptr ||
        SpanWidth(*ancestor.span) < SpanWidth(*innermost)) {
      innermost = ancestor.span;
    }
  }
  return innermost;
}

bool ComputeDynamicContext(
    const core::Span& current_span,
    std::span<const DynamicScopeAncestor> ancestors) {
  if (FindInnermostDynamic(current_span, ancestors) != nullptr) {
    return true;
  }
  return false;
}

}  // namespace cursive::analysis
