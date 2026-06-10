#pragma once

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

PatternTypeResult TypePatternAgainstType(const ScopeContext& ctx,
                                   const ast::PatternPtr& pattern,
                                   const TypeRef& expected);

bool IrrefutablePattern(const ScopeContext& ctx,
                        const ast::PatternPtr& pattern,
                        const TypeRef& expected);

// Spec §17.6.4 CoversVariant: true iff `pattern` is an enum pattern for a
// variant of `expected` whose payload subpatterns are all irrefutable.
bool EnumPatternCoversVariant(const ScopeContext& ctx,
                              const ast::PatternPtr& pattern,
                              const TypeRef& expected);

// Spec §17.6.4 CoversState: true iff `pattern` is a modal pattern for a state
// of `expected` whose payload field subpatterns are all irrefutable.
bool ModalPatternCoversState(const ScopeContext& ctx,
                             const ast::PatternPtr& pattern,
                             const TypeRef& expected);

}  // namespace ultraviolet::analysis
