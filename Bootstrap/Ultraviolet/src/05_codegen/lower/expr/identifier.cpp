// =============================================================================
// MIGRATION MAPPING: expr/identifier.cpp
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Lines 16057-16065: (Lower-Expr-Ident-Local) and (Lower-Expr-Ident-Path)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_places.cpp
//   - Lines 310-378: LowerReadPlace for IdentifierExpr
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRReadVar, IRReadPath, IRValue)
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult)
//
// REFACTORING NOTES:
//   1. Local binding: IRReadVar
//   2. Capture: Load from capture environment
//   3. Path/global: IRReadPath with resolution
//
// =============================================================================

#include "05_codegen/lower/expr/identifier.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "00_core/assert_spec.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/intrinsics/builtins.h"

namespace ultraviolet::codegen {

namespace {

LowerResult LowerLocalIdentifierRead(const ast::Expr& expr,
                                     const BindingState& binding,
                                     const std::string& ir_name,
                                     LowerCtx& ctx) {
    IRReadVar read;
    read.name = ir_name;

    IRValue value;
    value.kind = IRValue::Kind::Local;
    value.name = ir_name;
    if (binding.type) {
        ctx.RegisterValueType(value, binding.type);
    } else if (ctx.expr_type) {
        ctx.RegisterValueType(value, ctx.expr_type(expr));
    }

    IRPtr key_ir = LowerImplicitKeyAccess(expr, ast::KeyMode::Read, ctx);
    return LowerResult{SeqIR({key_ir, MakeIR(std::move(read))}), value};
}

LowerResult LowerCaptureIdentifierRead(const ast::Expr& expr,
                                       const CaptureAccess& capture,
                                       LowerCtx& ctx) {
    IRPtr ir = EmptyIR();

    IRValue field_ptr = ctx.CaptureFieldPtr(capture);
    IRValue value = ctx.FreshTempValue("capture_val");

    if (capture.by_ref) {
        IRValue captured_ptr = ctx.FreshTempValue("capture_ptr");

        IRReadPtr load_ptr;
        load_ptr.ptr = field_ptr;
        load_ptr.result = captured_ptr;
        ctx.RegisterValueType(captured_ptr, capture.field_type);

        IRReadPtr load_val;
        load_val.ptr = captured_ptr;
        load_val.result = value;

        ir = SeqIR({MakeIR(std::move(load_ptr)), MakeIR(std::move(load_val))});
    } else {
        IRReadPtr load_val;
        load_val.ptr = field_ptr;
        load_val.result = value;

        ir = MakeIR(std::move(load_val));
    }

    if (ctx.expr_type) {
        ctx.RegisterValueType(value, ctx.expr_type(expr));
    } else {
        ctx.RegisterValueType(value, capture.value_type);
    }

    IRPtr key_ir = LowerImplicitKeyAccess(expr, ast::KeyMode::Read, ctx);
    return LowerResult{SeqIR({key_ir, ir}), value};
}

LowerResult LowerStaticIdentifierRead(const ast::Expr& expr,
                                      std::vector<std::string> full,
                                      const std::string& resolved_name,
                                      LowerCtx& ctx) {
    IRReadPath read;
    read.path = full;
    read.name = resolved_name;

    IRValue value;
    value.kind = IRValue::Kind::Symbol;
    value.name = resolved_name;

    if (!full.empty()) {
        const std::string qualified = full.back() + "::" + resolved_name;
        if (const std::string builtin = BuiltinSym(qualified); !builtin.empty()) {
            value.name = builtin;
        }
    }

    if (full.empty()) {
        if (const std::string builtin = BuiltinSym(resolved_name); !builtin.empty()) {
            value.name = builtin;
        }
    }

    if (ctx.expr_type) {
        ctx.RegisterValueType(value, ctx.expr_type(expr));
    }

    IRPtr key_ir = LowerImplicitKeyAccess(expr, ast::KeyMode::Read, ctx);
    return LowerResult{SeqIR({key_ir, MakeIR(std::move(read))}), value};
}

}  // namespace

// =============================================================================
// LowerIdentifier - Lower an identifier expression to IR
// =============================================================================
// SPEC: (Lower-Expr-Ident-Local)
//   Gamma |- ResolveValueName(x) => ent    ent.origin_opt = bottom
//   Gamma |- LowerReadPlace(Identifier(x)) => <IR, v>
//   ------------------------------------------------------------------
//   Gamma |- LowerExpr(Identifier(x)) => <IR, v>
//
// SPEC: (Lower-Expr-Ident-Path)
//   Gamma |- ResolveValueName(x) => ent    ent.origin_opt = mp
//   name = (ent.target_opt if present, else x)    PathOfModule(mp) = path
//   ------------------------------------------------------------------
//   Gamma |- LowerExpr(Identifier(x)) => <ReadPathIR(path, name), v>
//
// Identifier expressions are lowered based on how the name resolves:
//   1. Local bindings produce IRReadVar with a local IRValue
//   2. Captures produce IRReadPtr loads from the capture environment
//   3. Global/static names produce IRReadPath with a symbol IRValue
// =============================================================================

LowerResult LowerIdentifier(const ast::Expr& expr,
                            const ast::IdentifierExpr& ident,
                            LowerCtx& ctx) {
    const std::string& name = ident.name;
    const bool is_unqualified_builtin = !BuiltinSym(name).empty();

    if (ctx.lowering_contract_postcondition) {
        const auto entry_it = ctx.contract_param_entry_values.find(name);
        if (entry_it != ctx.contract_param_entry_values.end()) {
            const IRValue mapped = entry_it->second;
            if (mapped.kind == IRValue::Kind::Local) {
                IRReadVar read;
                read.name = mapped.name;
                if (ctx.expr_type) {
                    ctx.RegisterValueType(mapped, ctx.expr_type(expr));
                }
                return LowerResult{MakeIR(std::move(read)), mapped};
            }
            if (ctx.expr_type) {
                ctx.RegisterValueType(mapped, ctx.expr_type(expr));
            }
            return LowerResult{EmptyIR(), mapped};
        }
    }

    if (auto alias = ctx.LookupLocalAddrAlias(name)) {
        switch (alias->kind) {
            case LocalAddrAlias::Kind::Binding: {
                const BindingState* binding =
                    ctx.GetBindingStateById(alias->binding_name, alias->binding_id);
                if (!binding) {
                    ctx.ReportCodegenFailure();
                    return LowerResult{EmptyIR(), IRValue{}};
                }
                const std::string ir_name =
                    alias->stable_name.empty() ? alias->binding_name
                                               : alias->stable_name;
                return LowerLocalIdentifierRead(expr, *binding, ir_name, ctx);
            }
            case LocalAddrAlias::Kind::Capture: {
                const auto* capture = ctx.LookupCapture(alias->capture_name);
                if (!capture) {
                    ctx.ReportCodegenFailure();
                    return LowerResult{EmptyIR(), IRValue{}};
                }
                return LowerCaptureIdentifierRead(expr, *capture, ctx);
            }
            case LocalAddrAlias::Kind::Static:
                return LowerStaticIdentifierRead(expr,
                                                 alias->static_path,
                                                 alias->static_name,
                                                 ctx);
        }
    }

    // Case 1: Local binding - check if binding exists in scope
    if (const BindingState* binding = ctx.GetBindingState(name)) {
        SPEC_RULE("Lower-Expr-Ident-Local");
        const std::string ir_name =
            binding->stable_name.empty() ? name : binding->stable_name;
        return LowerLocalIdentifierRead(expr, *binding, ir_name, ctx);
    }

    // Case 2: Capture - check if identifier is captured in spawn/dispatch body
    if (const auto* capture = ctx.LookupCapture(name)) {
        SPEC_RULE("Lower-Expr-Ident-Capture");
        return LowerCaptureIdentifierRead(expr, *capture, ctx);
    }

    // Case 3: Path/global - resolve name to module path
    SPEC_RULE("Lower-Expr-Ident-Path");

    std::vector<std::string> full;
    std::string resolved_name = name;

    if (!ctx.resolve_name) {
        // No resolver available: builtins still lower directly to runtime symbols.
        if (is_unqualified_builtin) {
            full.clear();
        } else {
            ctx.ReportResolveFailure(name);
            full = ctx.module_path;
        }
    } else {
        // Attempt to resolve the name to a full path
        auto resolved = ctx.resolve_name(name);
        if (!resolved.has_value() || resolved->empty()) {
            // Unqualified builtins are globally available.
            if (is_unqualified_builtin) {
                full.clear();
            } else {
                ctx.ReportResolveFailure(name);
                full = ctx.module_path;
            }
        } else {
            // Resolution succeeded - extract module path and final name
            full = *resolved;
            resolved_name = full.back();
            full.pop_back();
        }
    }

    return LowerStaticIdentifierRead(expr, std::move(full), resolved_name, ctx);
}

}  // namespace ultraviolet::codegen
