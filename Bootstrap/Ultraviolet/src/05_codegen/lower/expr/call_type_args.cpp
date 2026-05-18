// =============================================================================
// MIGRATION MAPPING: expr/call_type_args.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 6.4 (Expression Lowering)
//   - Generic call instantiation with type arguments
//   - Lines 16143-16151: Call lowering with monomorphization
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/lower/lower_expr_calls.cpp
//   - Handles Call<T, U>(args) syntax with type parameters
//   - Resolves generic procedure instantiation
//
// DEPENDENCIES:
//   - ultraviolet/include/05_codegen/lower/lower_expr.h (LowerResult, LowerCtx)
//   - ultraviolet/include/05_codegen/ir/ir_model.h (IRCall, IRValue)
//   - ultraviolet/include/05_codegen/symbols/mangle.h (ScopedSym)
//   - ultraviolet/include/05_codegen/abi/abi.h (NeedsPanicOut, kPanicOutName)
//   - ultraviolet/include/04_analysis/generics/monomorphize.h (TypeSubst, InstantiateType)
//   - ultraviolet/include/04_analysis/typing/types.h (TypeRef)
//
// =============================================================================

#include "05_codegen/lower/expr/call_type_args.h"

#include <string>
#include <variant>
#include <vector>

#include "00_core/assert_spec.h"
#include "00_core/symbols.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/typing/type_expr.h"
#include "04_analysis/typing/type_lower.h"
#include "04_analysis/typing/types.h"
#include "05_codegen/abi/abi.h"
#include "05_codegen/cleanup/cleanup.h"
#include "05_codegen/checks/panic.h"
#include "05_codegen/ir/ir_model.h"
#include "05_codegen/symbols/mangle.h"

namespace ultraviolet::codegen {

namespace {

// =============================================================================
// Helper functions for generic call lowering
// =============================================================================

// Extract type arguments from the call expression's generic_args field.
// Lowers each AST type to an analysis TypeRef.
std::vector<analysis::TypeRef> GetCallTypeArgs(
    const std::vector<ast::TypePtr>& generic_args,
    LowerCtx& ctx) {
  SPEC_DEF("GetCallTypeArgs", "6.4");

  std::vector<analysis::TypeRef> type_args;
  type_args.reserve(generic_args.size());

  const analysis::ScopeContext& scope_ctx = ScopeForLowering(ctx);

  for (const auto& ast_type : generic_args) {
    if (!ast_type) {
      type_args.push_back(nullptr);
      continue;
    }

    auto result = analysis::LowerType(scope_ctx, ast_type);
    if (result.ok && result.type) {
      type_args.push_back(result.type);
    } else {
      // Type lowering failed; push nullptr and continue
      type_args.push_back(nullptr);
    }
  }

  return type_args;
}

// Encode a single type into path segments for mangling.
// This produces a deterministic encoding suitable for symbol mangling.
std::vector<std::string> EncodeTypeForMangle(const analysis::TypeRef& type) {
  if (!type) {
    return {"_"};
  }

  return std::visit(
      [](const auto& node) -> std::vector<std::string> {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, analysis::TypePrim>) {
          return {"P", node.name};
        }
        else if constexpr (std::is_same_v<T, analysis::TypePathType>) {
          std::vector<std::string> result = {"T"};
          for (const auto& seg : node.path) {
            result.push_back(seg);
          }
          // Recursively encode generic args if present
          if (!node.generic_args.empty()) {
            result.push_back("G");
            for (const auto& arg : node.generic_args) {
              auto arg_enc = EncodeTypeForMangle(arg);
              result.insert(result.end(), arg_enc.begin(), arg_enc.end());
            }
            result.push_back("E");
          }
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeTuple>) {
          std::vector<std::string> result = {"Tu"};
          for (const auto& elem : node.elements) {
            auto elem_enc = EncodeTypeForMangle(elem);
            result.insert(result.end(), elem_enc.begin(), elem_enc.end());
          }
          result.push_back("E");
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeArray>) {
          auto elem_enc = EncodeTypeForMangle(node.element);
          std::vector<std::string> result = {"A", std::to_string(node.length)};
          result.insert(result.end(), elem_enc.begin(), elem_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeSlice>) {
          auto elem_enc = EncodeTypeForMangle(node.element);
          std::vector<std::string> result = {"S"};
          result.insert(result.end(), elem_enc.begin(), elem_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypePtr>) {
          auto elem_enc = EncodeTypeForMangle(node.element);
          std::vector<std::string> result = {"Pt"};
          result.insert(result.end(), elem_enc.begin(), elem_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeRawPtr>) {
          auto elem_enc = EncodeTypeForMangle(node.element);
          std::vector<std::string> result;
          if (node.qual == analysis::RawPtrQual::Mut) {
            result.push_back("Rm");
          } else {
            result.push_back("Ri");
          }
          result.insert(result.end(), elem_enc.begin(), elem_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeUnion>) {
          std::vector<std::string> result = {"U"};
          for (const auto& member : node.members) {
            auto mem_enc = EncodeTypeForMangle(member);
            result.insert(result.end(), mem_enc.begin(), mem_enc.end());
          }
          result.push_back("E");
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeFunc>) {
          std::vector<std::string> result = {"F"};
          for (const auto& param : node.params) {
            auto param_enc = EncodeTypeForMangle(param.type);
            result.insert(result.end(), param_enc.begin(), param_enc.end());
          }
          result.push_back("R");
          auto ret_enc = EncodeTypeForMangle(node.ret);
          result.insert(result.end(), ret_enc.begin(), ret_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypePerm>) {
          std::vector<std::string> result;
          switch (node.perm) {
            case analysis::Permission::Const:
              result.push_back("Pc");
              break;
            case analysis::Permission::Unique:
              result.push_back("Pu");
              break;
            case analysis::Permission::Shared:
              result.push_back("Ps");
              break;
          }
          auto base_enc = EncodeTypeForMangle(node.base);
          result.insert(result.end(), base_enc.begin(), base_enc.end());
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeModalState>) {
          std::vector<std::string> result = {"M"};
          for (const auto& seg : node.path) {
            result.push_back(seg);
          }
          result.push_back("@");
          result.push_back(node.state);
          if (!node.generic_args.empty()) {
            result.push_back("G");
            for (const auto& arg : node.generic_args) {
              auto arg_enc = EncodeTypeForMangle(arg);
              result.insert(result.end(), arg_enc.begin(), arg_enc.end());
            }
            result.push_back("E");
          }
          return result;
        }
        else if constexpr (std::is_same_v<T, analysis::TypeString>) {
          if (node.state.has_value()) {
            if (*node.state == analysis::StringState::Managed) {
              return {"Sm"};
            }
            return {"Sv"};
          }
          return {"Sx"};
        }
        else if constexpr (std::is_same_v<T, analysis::TypeBytes>) {
          if (node.state.has_value()) {
            if (*node.state == analysis::BytesState::Managed) {
              return {"Bm"};
            }
            return {"Bv"};
          }
          return {"Bx"};
        }
        else if constexpr (std::is_same_v<T, analysis::TypeDynamic>) {
          std::vector<std::string> result = {"D"};
          for (const auto& seg : node.path) {
            result.push_back(seg);
          }
          return result;
        }
        else {
          // TypeOpaque, TypeRefine, and range-family forms - encode as opaque
          return {"X"};
        }
      },
      type->node);
}

// Generate a monomorphized symbol name for a generic procedure instantiation.
// Combines the base procedure path with encoded type arguments.
std::string MonomorphizedSym(
    const std::vector<std::string>& base_path,
    const std::vector<analysis::TypeRef>& type_args) {
  SPEC_DEF("MonomorphizedSym", "6.3.1");

  if (type_args.empty()) {
    // No type arguments; return the base symbol
    return ScopedSym(base_path);
  }

  // Build the instantiation path: base_path + ["inst"] + encoded type args
  std::vector<std::string> inst_path = base_path;
  inst_path.push_back("inst");

  for (const auto& type_arg : type_args) {
    auto encoded = EncodeTypeForMangle(type_arg);
    inst_path.insert(inst_path.end(), encoded.begin(), encoded.end());
  }

  return ScopedSym(inst_path);
}

// Extract the callee path from a call expression's callee.
// Returns the procedure path if resolvable.
std::optional<std::vector<std::string>> ExtractCalleePath(
    const ast::ExprPtr& callee,
    LowerCtx& ctx) {
  if (!callee) {
    return std::nullopt;
  }

  return std::visit(
      [&ctx](const auto& node) -> std::optional<std::vector<std::string>> {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          // Try to resolve the identifier to a full path
          if (ctx.resolve_name) {
            if (auto resolved = ctx.resolve_name(node.name)) {
              return *resolved;
            }
          }
          // Fall back to module path + name
          std::vector<std::string> path = ctx.module_path;
          path.push_back(node.name);
          return path;
        }
        else if constexpr (std::is_same_v<T, ast::PathExpr>) {
          std::vector<std::string> path = node.path;
          path.push_back(node.name);
          return path;
        }
        else if constexpr (std::is_same_v<T, ast::QualifiedNameExpr>) {
          std::vector<std::string> path = node.path;
          path.push_back(node.name);
          return path;
        }
        else {
          // Other expression types don't have a simple path extraction
          return std::nullopt;
        }
      },
      callee->node);
}

}  // namespace

// =============================================================================
// Section 6.4 LowerCallWithTypeArgs - lower call expression with type arguments
// =============================================================================
//
// (Lower-Call-TypeArgs)
//   Gamma |- GetCallTypeArgs(expr.generic_args) => type_args
//   Gamma |- ExtractCalleePath(expr.callee) => base_path
//   sym = MonomorphizedSym(base_path, type_args)
//   Gamma |- LowerArgs(params, args) => <IR_a, vec_v>
//   -------------------------------------------------------------------------
//   Gamma |- LowerCallWithTypeArgs(Call<...>(args)) => <SeqIR(IR_a, CallIR(sym, vec_v)), v>
//
// When a call has explicit type arguments (e.g., foo<i32, bool>(x, y)),
// the callee symbol must be monomorphized:
// 1. Extract type arguments from the call expression
// 2. Resolve the base procedure path from the callee
// 3. Generate a monomorphized symbol incorporating the type arguments
// 4. Lower arguments normally
// 5. Create IRCall with the instantiated symbol as callee

LowerResult LowerCallWithTypeArgs(const ast::CallExpr& expr, LowerCtx& ctx) {
  SPEC_RULE("Lower-Call-TypeArgs");

  // 1. Extract and lower type arguments
  auto type_args = GetCallTypeArgs(expr.generic_args, ctx);

  // 2. Extract the callee path
  auto callee_path_opt = ExtractCalleePath(expr.callee, ctx);

  std::string callee_sym;
  if (callee_path_opt.has_value() && !type_args.empty()) {
    // 3. Generate monomorphized symbol
    callee_sym = MonomorphizedSym(*callee_path_opt, type_args);
  } else if (callee_path_opt.has_value()) {
    // No type args; use base symbol
    callee_sym = ScopedSym(*callee_path_opt);
  } else {
    // Cannot extract path; lower callee as expression and use its value
    auto callee_result = LowerExpr(*expr.callee, ctx);

    // Extract parameter modes from callee type if available
    ParamModeList param_modes;
    ParamTypeList param_types;
    if (ctx.expr_type) {
      auto callee_type = ctx.expr_type(*expr.callee);
      if (callee_type) {
        auto stripped = analysis::StripPerm(callee_type);
        if (stripped) {
          if (const auto* func =
                  std::get_if<analysis::TypeFunc>(&stripped->node)) {
            param_modes.reserve(func->params.size());
            param_types.reserve(func->params.size());
            for (const auto& param : func->params) {
              param_modes.push_back(param.mode);
              param_types.push_back(param.type);
            }
          }
        }
      }
    }

    // Lower arguments
    auto [args_ir, arg_values] =
        LowerArgs(param_modes,
                  expr.args,
                  ctx,
                  param_types.empty() ? nullptr : &param_types);

    IRValue result_value = ctx.FreshTempValue("call_generic");

    IRCall call;
    call.callee = callee_result.value;
    call.args = std::move(arg_values);
    call.result = result_value;

    // Check if panic out is needed
    bool needs_panic_out = true;
    if (call.callee.kind == IRValue::Kind::Symbol) {
      needs_panic_out = ctx.NeedsPanicOutForSymbol(call.callee.name);
    }

    if (needs_panic_out) {
      IRValue panic_out;
      panic_out.kind = IRValue::Kind::Local;
      panic_out.name = std::string(kPanicOutName);
      call.args.push_back(panic_out);
      return LowerResult{
          SeqIR({callee_result.ir, args_ir, MakeIR(std::move(call)),
                 PanicFollowup(ctx)}),
          result_value};
    }

    return LowerResult{
        SeqIR({callee_result.ir, args_ir, MakeIR(std::move(call))}),
        result_value};
  }

  // 4. Lower arguments
  ParamModeList param_modes;
  ParamTypeList param_types;
  if (ctx.expr_type) {
    auto callee_type = ctx.expr_type(*expr.callee);
    if (callee_type) {
      auto stripped = analysis::StripPerm(callee_type);
      if (stripped) {
        if (const auto* func =
                std::get_if<analysis::TypeFunc>(&stripped->node)) {
          param_modes.reserve(func->params.size());
          param_types.reserve(func->params.size());
          for (const auto& param : func->params) {
            param_modes.push_back(param.mode);
            param_types.push_back(param.type);
          }
        }
      }
    }
  }

  auto [args_ir, arg_values] =
      LowerArgs(param_modes,
                expr.args,
                ctx,
                param_types.empty() ? nullptr : &param_types);

  // 5. Create IRCall with the monomorphized symbol
  IRValue result_value = ctx.FreshTempValue("call_generic");

  IRCall call;
  call.callee = IRValue{IRValue::Kind::Symbol, callee_sym, {}};
  call.args = std::move(arg_values);
  call.result = result_value;

  // Determine if we need to add panic out parameter
  bool needs_panic_out = ctx.NeedsPanicOutForSymbol(callee_sym);

  if (needs_panic_out) {
    SPEC_RULE("Lower-Call-TypeArgs-PanicOut");
    IRValue panic_out;
    panic_out.kind = IRValue::Kind::Local;
    panic_out.name = std::string(kPanicOutName);
    call.args.push_back(panic_out);
    return LowerResult{
        SeqIR({args_ir, MakeIR(std::move(call)), PanicFollowup(ctx)}),
        result_value};
  }

  SPEC_RULE("Lower-Call-TypeArgs-NoPanicOut");
  return LowerResult{SeqIR({args_ir, MakeIR(std::move(call))}), result_value};
}

// =============================================================================
// HasExplicitTypeArgs - check if a call expression has explicit type arguments
// =============================================================================

bool HasExplicitTypeArgs(const ast::CallExpr& expr) {
  return !expr.generic_args.empty();
}

// =============================================================================
// Anchor function for SPEC_RULE markers
// =============================================================================

void AnchorCallTypeArgsRules() {
  SPEC_RULE("Lower-Call-TypeArgs");
  SPEC_RULE("Lower-Call-TypeArgs-PanicOut");
  SPEC_RULE("Lower-Call-TypeArgs-NoPanicOut");
}

}  // namespace ultraviolet::codegen
