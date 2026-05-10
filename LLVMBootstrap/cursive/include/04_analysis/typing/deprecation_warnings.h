#pragma once

#include <optional>

#include "00_core/span.h"
#include "04_analysis/typing/type_stmt.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

void EmitDeprecatedReferenceWarningFromAttrs(
    const ast::AttributeList& attrs_list,
    const StmtTypeContext& type_ctx,
    const std::optional<core::Span>& span);

}  // namespace cursive::analysis

