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

}  // namespace ultraviolet::analysis
