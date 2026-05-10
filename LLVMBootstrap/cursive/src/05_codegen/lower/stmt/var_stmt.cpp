// =============================================================================
// MIGRATION MAPPING: stmt/var_stmt.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md Lines 16624-16627 (Lower-Stmt-Var)
//   - Same structure as Lower-Stmt-Let but registers mutable binding
//   - LowerExpr(init) produces IR_i, v
//   - LowerBindPattern(pattern, v) produces IR_b
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/lower/lower_stmt.cpp
//   - Lines 317-360: LowerVarStmt function
//   - Nearly identical to LowerLetStmt
//   - Registers variable as mutable in context
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/ir/ir_model.h (IRBindVar)
//   - cursive/include/05_codegen/lower/lower_expr.h (LowerCtx, LowerResult)
//   - cursive/include/05_codegen/lower/lower_pat.h (LowerBindPattern)
//   - cursive/include/04_analysis/layout/layout.h (LowerTypeForLayout)
//
// REFACTORING NOTES:
//   - Consider unifying with let_stmt.cpp via shared helper
//
// =============================================================================

#include "05_codegen/lower/stmt/var_stmt.h"

#include "00_core/assert_spec.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/lower/expr/expr_common.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/lower/lower_pat.h"

namespace cursive::codegen {

namespace {

// ============================================================================
// Provenance tracking helpers
// ============================================================================

struct ProvInfo {
  analysis::ProvenanceKind kind = analysis::ProvenanceKind::Bottom;
  std::optional<std::string> region;
  std::optional<std::string> region_tag;
  bool fresh_region = false;
};

// Derive binding provenance from initializer provenance.
// If initializer has Bottom provenance, the binding gets Stack provenance.
ProvInfo BindProvInfo(const ProvInfo& init) {
  if (init.kind == analysis::ProvenanceKind::Bottom) {
    return ProvInfo{analysis::ProvenanceKind::Stack, std::nullopt, std::nullopt, false};
  }
  return init;
}

// Extract provenance info from an expression using context lookups.
ProvInfo ExprProvInfo(const ast::Expr& expr, const LowerCtx& ctx) {
  ProvInfo info;
  if (auto prov = ctx.LookupExprProv(expr)) {
    info.kind = *prov;
  }
  if (info.kind == analysis::ProvenanceKind::Region) {
    info.region = ctx.LookupExprRegion(expr);
    info.region_tag = ctx.LookupExprRegionTag(expr);
  }
  return info;
}

// Lower an optional type annotation to a TypeRef.
analysis::TypeRef LowerBindingType(const ast::TypePtr& type_opt,
                                   LowerCtx& ctx) {
  if (!type_opt) {
    return nullptr;
  }
  const analysis::ScopeContext& scope = ScopeForLowering(ctx);
  if (const auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, type_opt)) {
    return *lowered;
  }
  return nullptr;
}

std::optional<std::string> SimplePatternBindingName(const ast::PatternPtr& pat) {
  if (!pat) {
    return std::nullopt;
  }
  if (const auto* ident = std::get_if<ast::IdentifierPattern>(&pat->node)) {
    return ident->name;
  }
  if (const auto* typed = std::get_if<ast::TypedPattern>(&pat->node)) {
    if (typed->name == "_") {
      return std::nullopt;
    }
    return typed->name;
  }
  return std::nullopt;
}

}  // namespace

// ============================================================================
// Lower-Stmt-Var
// ============================================================================
//
// Per the spec (Lines 16624-16627):
//   BindingParts(binding) = <pat, ty_opt, op, init, span>
//   LowerExpr(init) => <IR_i, v>
//   LowerBindPattern(pat, v) => IR_b
//   Result: SeqIR(IR_i, IR_b)
//
// The implementation handles:
//   - Immovable bindings (:= operator)
//   - Optional pattern (anonymous binding if no pattern)
//   - Provenance tracking for region safety
//   - Temp suppression during initializer lowering
//
// Note: This is structurally identical to LowerLetStmt. The distinction
// between let (immutable) and var (mutable) is semantic and handled by
// the type system and borrow checker, not by the IR lowering.
//
IRPtr LowerVarStmt(const ast::VarStmt& stmt, LowerCtx& ctx) {
  SPEC_RULE("Lower-Stmt-Var");

  const auto& binding = stmt.binding;

  // Early exit if no initializer
  if (!binding.init) {
    return EmptyIR();
  }

  // Suppress temp registration at this depth to avoid creating temps
  // for the initializer expression itself (the binding takes responsibility).
  auto prev_suppress = ctx.suppress_temp_at_depth;
  ctx.suppress_temp_at_depth = ctx.temp_depth + 1;
  auto init_result = LowerExpr(*binding.init, ctx);
  ctx.suppress_temp_at_depth = prev_suppress;

  IRPtr bind_ir = EmptyIR();

  // Determine the binding type:
  // 1. From explicit type annotation if present
  // 2. From expression type inference if available
  analysis::TypeRef var_type;
  var_type = LowerBindingType(ast::BindingAnnotationTypeOpt(binding), ctx);
  if (!var_type && ctx.expr_type && binding.init) {
    var_type = ctx.expr_type(*binding.init);
  }
  if (!var_type) {
    var_type = ctx.LookupValueType(init_result.value);
  }

  // Implicit dynamic widening: when the binding type is $ClassName (TypeDynamic)
  // and the initializer is a concrete record type, we need to create a dense
  // pointer {data_ptr, vtable_ptr} via DynPack.
  if (var_type && ctx.sigma && binding.init) {
    analysis::TypeRef stripped_var = analysis::StripPerm(var_type);
    if (!stripped_var) {
      stripped_var = var_type;
    }
    if (stripped_var &&
        std::holds_alternative<analysis::TypeDynamic>(stripped_var->node)) {
      const auto& dyn = std::get<analysis::TypeDynamic>(stripped_var->node);
      analysis::PathKey class_key;
      for (const auto& seg : dyn.path) {
        class_key.push_back(seg);
      }
      const auto class_it = ctx.sigma->classes.find(class_key);
      if (class_it != ctx.sigma->classes.end()) {
        // Prefer the lowered IR value type over contextual AST type. The AST
        // expression type may already be context-adjusted to $ClassName, which
        // would incorrectly suppress required DynPack materialization.
        analysis::TypeRef expr_type = ctx.LookupValueType(init_result.value);
        if (!expr_type && ctx.expr_type) {
          expr_type = ctx.expr_type(*binding.init);
        }
        analysis::TypeRef stripped_expr =
            expr_type ? analysis::StripPerm(expr_type) : nullptr;
        if (!stripped_expr) {
          stripped_expr = expr_type;
        }
        // Only widen if the expression type is NOT already dynamic
        const bool already_dynamic =
            stripped_expr &&
            std::holds_alternative<analysis::TypeDynamic>(stripped_expr->node);
        if (stripped_expr && !already_dynamic) {
          DynPackResult pack = DynPack(
              stripped_expr, *binding.init, dyn.path, class_it->second, ctx);
          IRValue dyn_value = ctx.FreshTempValue("dyn_widen");
          // DynPack materializes through DerivedValueInfo::DynLit in LLVM emission.
          // The carrier must be Opaque (not Local) because there is no SSA slot
          // backing this value before pattern binding.
          dyn_value.kind = IRValue::Kind::Opaque;
          dyn_value.vtable_sym = pack.vtable_sym;
          DerivedValueInfo info;
          info.kind = DerivedValueInfo::Kind::DynLit;
          info.base = pack.data_ptr;
          info.vtable_sym = pack.vtable_sym;
          info.dyn_impl_type = stripped_expr;
          info.dyn_class_path = dyn.path;
          ctx.RegisterDerivedValue(dyn_value, info);
          init_result = LowerResult{SeqIR({init_result.ir, pack.ir}), dyn_value};
        }
      }
    }
  }

  // Compute provenance for the binding
  ProvInfo init_prov;
  if (binding.init) {
    init_prov = ExprProvInfo(*binding.init, ctx);
  }
  const ProvInfo bind_prov = BindProvInfo(init_prov);

  // Check if this is an immovable binding (:= operator)
  const bool immovable = binding.op.lexeme == ":=";

  if (binding.pat) {
    // Pattern binding: register variables from pattern, then lower the binding
    RegisterPatternBindings(*binding.pat, var_type, ctx, immovable,
                            bind_prov.kind, bind_prov.region,
                            bind_prov.region_tag);
    bind_ir = LowerBindPattern(*binding.pat, init_result.value, ctx);
  } else {
    // Anonymous binding: create a single binding with synthetic name
    IRBindVar bind;
    bind.name = "anon_var";
    bind.value = init_result.value;
    bind.type = var_type;
    bind.prov = bind_prov.kind;
    bind.prov_region = bind_prov.region;
    bind.prov_region_tag = bind_prov.region_tag;
    bind_ir = MakeIR(std::move(bind));
    ctx.RegisterVar(bind.name, var_type, true, immovable, bind_prov.kind,
                    bind_prov.region, false, bind_prov.region_tag);
  }

  IRValue checked_value = init_result.value;
  if (binding.pat) {
    if (const auto simple_name = SimplePatternBindingName(binding.pat)) {
      checked_value.kind = IRValue::Kind::Local;
      checked_value.name = *simple_name;
      if (var_type) {
        ctx.RegisterValueType(checked_value, var_type);
      }
    }
  } else {
    checked_value.kind = IRValue::Kind::Local;
    checked_value.name = "anon_var";
    if (var_type) {
      ctx.RegisterValueType(checked_value, var_type);
    }
  }

  IRPtr refine_ir = EmptyIR();
  if (binding.init) {
    refine_ir =
        EmitDynamicRefinementChecksForExpr(*binding.init, checked_value, var_type, ctx);
  }
  return SeqIR({init_result.ir, bind_ir, refine_ir});
}

}  // namespace cursive::codegen
