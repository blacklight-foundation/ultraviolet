#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "00_core/span.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

constexpr std::uint64_t kWidenLargePayloadThresholdBytes = 256;

std::optional<std::string_view> PayloadState(const ScopeContext& ctx,
                                             const ast::ModalDecl& decl);

bool NicheApplies(const ScopeContext& ctx, const ast::ModalDecl& decl);

bool NicheCompatible(const ScopeContext& ctx,
                     const TypePath& modal_path,
                     std::string_view state);

bool WidenWarnCond(const ScopeContext& ctx,
                   const TypePath& modal_path,
                   std::string_view state);

// Emit warning diagnostic for large payload widening
void WarnWidenLargePayload(const ScopeContext& ctx,
                           const StmtTypeContext& type_ctx,
                           const core::Span& span,
                           const TypePath& modal_path,
                           std::string_view state);

}  // namespace cursive::analysis
