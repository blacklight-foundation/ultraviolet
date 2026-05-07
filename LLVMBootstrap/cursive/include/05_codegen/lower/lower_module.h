#pragma once

#include <vector>
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "02_source/ast/ast.h"

namespace cursive::codegen {

// Register module-level symbol/type metadata needed for cross-module codegen
// resolution (procedure signatures, static symbols, foreign contracts, and
// module init/deinit signatures) without lowering procedure bodies.
bool RegisterModuleSignatures(const ast::ASTModule& module, LowerCtx& ctx);

// LowerModule - Lower an entire module to IR declarations
IRDecls LowerModule(const ast::ASTModule& module,
                    LowerCtx& ctx);

// ExpandIR - append generated declarations required by the lowered IR.
IRDecls ExpandIR(const IRDecls& decls,
                 LowerCtx& ctx);

}  // namespace cursive::codegen
