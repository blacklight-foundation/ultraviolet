#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// ============================================================================
// §6.x Procedure Lowering
// ============================================================================

// LowerProc - Lower a procedure declaration to ProcIR
// Handles parameter binding, body lowering, and return handling.
ProcIR LowerProc(const ast::ProcedureDecl& decl,
                 const ast::ModulePath& module_path,
                 LowerCtx& ctx,
                 std::optional<std::string> symbol_override = std::nullopt);

// Lower a generic procedure as a concrete instantiation.
ProcIR LowerProcInstantiated(const ast::ProcedureDecl& decl,
                             const ast::ModulePath& module_path,
                             const std::string& symbol_override,
                             const std::map<std::string, analysis::TypeRef>& type_subst,
                             LowerCtx& ctx);

// AnchorProcRules - Emit SPEC_RULE anchors
void AnchorProcRules();

}  // namespace ultraviolet::codegen
