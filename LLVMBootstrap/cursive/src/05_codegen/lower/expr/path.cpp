// =============================================================================
// Expression Lowering: PathExpr
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Section 6.4 (Expression Lowering)
//   - Line 16067-16069: (Lower-Expr-Path)
//
// MIGRATED FROM:
//   - cursive-bootstrap/src/04_codegen/lower/lower_expr_core.cpp
//   - Lines 1175-1188: LowerPath function
//
// =============================================================================

#include "05_codegen/lower/expr/path.h"
#include "00_core/assert_spec.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/builtins.h"

namespace cursive::codegen {

// =============================================================================
// LowerPath - Lower a path expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Path)
//   PathOfModule(mp) = path    name = last(path)    path' = init(path)
//   -----------------------------------------------------------------
//   Gamma |- LowerExpr(Path(mp, name)) => <ReadPathIR(path', name), v>
//
// Path expressions reference items via a qualified path (e.g., std::io::File).
// They produce:
// 1. IRReadPath to read from the specified module path
// 2. A symbol IRValue with the final name
//
// Unlike IdentifierExpr, PathExpr already has the full module path resolved.
// =============================================================================

LowerResult LowerPath(const ast::PathExpr& expr, LowerCtx& ctx) {
    SPEC_RULE("Lower-Expr-Path");

    // Build IRReadPath from the path components
    IRReadPath read;
    read.path = expr.path;  // Module path segments
    read.name = expr.name;  // Final item name

    // Create symbol IRValue
    IRValue value;
    value.kind = IRValue::Kind::Symbol;
    value.name = expr.name;

    // Builtin path calls (for example string::length, bytes::to_managed)
    // lower to runtime builtin symbols rather than user-scope path symbols.
    if (!expr.path.empty()) {
        const std::string qualified = expr.path.back() + "::" + expr.name;
        if (const std::string builtin = BuiltinSym(qualified); !builtin.empty()) {
            value.name = builtin;
        }
    }

    return LowerResult{MakeIR(std::move(read)), value};
}

}  // namespace cursive::codegen
