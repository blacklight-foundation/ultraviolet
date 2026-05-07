// =============================================================================
// MIGRATION MAPPING: dyn_dispatch.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.10 Dynamic Dispatch (lines 17163-17200)
//   - VTableEligible(Cl) definition (line 17165)
//   - vtable_eligible(m) predicate (line 11941)
//   - SelfOccurs predicate (§5.4.1)
//   - dispatchable(Cl) predicate (line 11943)
//   - VTable-Order rule (lines 17182-17185)
//   - VSlot-Entry rule (lines 17188-17190)
//   - DynPack rule (lines 17193-17195)
//   - LowerDynCall rule (lines 17198-17200)
//   - DispatchSym-Impl, DispatchSym-Default-None rules
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/dyn_dispatch.cpp
//   - Lines 1-100: SelfOccurs implementation
//   - Lines 104-120: SelfOccursInMethod
//   - Lines 122-166: IsVTableEligible, VTableEligible
//   - Lines 168-209: DispatchSym implementation
//   - Lines 211-251: VTable generation
//   - Lines 253-270: EmitVTable
//   - Lines 272-294: VSlot lookup
//   - Lines 296+: DynPack (fat pointer creation)
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/dyn_dispatch/dyn_dispatch.h
//   - cursive/include/05_codegen/ir/ir_model.h (GlobalVTable, CallVTable)
//   - cursive/include/05_codegen/symbols/mangle.h (MangleVTable, MangleMethod)
//   - cursive/include/04_analysis/layout/layout.h (SizeOf, AlignOf)
//   - cursive/include/05_codegen/cleanup/cleanup.h (DropGlueSym)
//   - cursive/include/04_analysis/composite/classes.h
//   - cursive/include/04_analysis/composite/record_methods.h
//
// REFACTORING NOTES:
//   1. VTable eligibility: HasReceiver(m) AND NOT SelfOccurs(m)
//   2. SelfOccurs checks if Self type appears in params/return
//   3. VTableEligible returns ordered list of eligible methods
//   4. VSlot returns 0-based index into vtable slots
//   5. DynPack creates fat pointer: (data_ptr, vtable_ptr)
//   6. DispatchSym resolves actual implementation symbol
//
// VTABLE LAYOUT:
//   VTable = {
//     size: usize,      // ::cursive::analysis::layout::SizeOf(T)
//     align: usize,     // ::cursive::analysis::layout::AlignOf(T)
//     drop: *ptr,       // DropGlueSym(T)
//     slots: [*ptr]     // Method pointers in VTableEligible order
//   }
//
// DYNAMIC CALL LOWERING:
//   1. Extract vtable pointer from fat pointer
//   2. Load slot pointer at VSlot(Cl, method) index
//   3. Call slot pointer with (data_ptr, args...)
//   4. Check panic out-parameter
//
// SELFOCCURS RECURSION:
//   - TypePath(["Self"]) -> true
//   - TypePerm(p, ty) -> SelfOccurs(ty)
//   - TypeTuple/Union -> any element
//   - TypeArray/Slice/Ptr/RawPtr -> element type
//   - TypeFunc -> params or return
//   - Primitives -> false
// =============================================================================

#include "05_codegen/dyn_dispatch/dyn_dispatch.h"

#include <variant>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/classes.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/typing/type_predicates.h"
#include "05_codegen/checks/checks.h"
#include "05_codegen/cleanup/cleanup.h"
#include "04_analysis/layout/layout.h"
#include "05_codegen/symbols/mangle.h"

namespace cursive::codegen {

// ============================================================================
// §5.4.1 SelfOccurs - Check if Self type occurs in a type
// ============================================================================

// Forward declaration
static bool SelfOccursInType(const ast::Type& type);

static bool SelfOccursInType(const std::shared_ptr<ast::Type>& type) {
  if (!type) {
    return false;
  }
  return SelfOccursInType(*type);
}

static bool SelfOccursInType(const ast::Type& type) {
  SPEC_DEF("SelfOccurs", "5.4.1");

  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;

        // SelfOccurs(TypePath(["Self"])) = true
        if constexpr (std::is_same_v<T, ast::TypePathType>) {
          return node.path.size() == 1 && node.path[0] == "Self";
        }
        // SelfOccurs(TypePerm(p, ty)) = SelfOccurs(ty)
        else if constexpr (std::is_same_v<T, ast::TypePermType>) {
          return SelfOccursInType(node.base);
        }
        // SelfOccurs(TypeTuple([...])) = any element SelfOccurs
        else if constexpr (std::is_same_v<T, ast::TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (SelfOccursInType(elem)) {
              return true;
            }
          }
          return false;
        }
        // SelfOccurs(TypeArray(ty, e)) = SelfOccurs(ty)
        else if constexpr (std::is_same_v<T, ast::TypeArray>) {
          return SelfOccursInType(node.element);
        }
        // SelfOccurs(TypeSlice(ty)) = SelfOccurs(ty)
        else if constexpr (std::is_same_v<T, ast::TypeSlice>) {
          return SelfOccursInType(node.element);
        }
        // SelfOccurs(TypePtr(_, _)) = false
        else if constexpr (std::is_same_v<T, ast::TypeSafePtr>) {
          return false;
        }
        // SelfOccurs(TypeRawPtr(_, _)) = false
        else if constexpr (std::is_same_v<T, ast::TypeRawPtr>) {
          return false;
        }
        // SelfOccurs(TypeFunc(...)) = any param or return SelfOccurs
        else if constexpr (std::is_same_v<T, ast::TypeFunc>) {
          for (const auto& param : node.params) {
            if (SelfOccursInType(param.type)) {
              return true;
            }
          }
          return SelfOccursInType(node.ret);
        }
        // SelfOccurs(TypeUnion([...])) = any member SelfOccurs
        else if constexpr (std::is_same_v<T, ast::TypeUnion>) {
          for (const auto& ty : node.types) {
            if (SelfOccursInType(ty)) {
              return true;
            }
          }
          return false;
        }
        // SelfOccurs(TypeModalState(_, _)) = false
        else if constexpr (std::is_same_v<T, ast::TypeModalState>) {
          return false;
        }
        // SelfOccurs(TypeRefine(base, pred)) = SelfOccurs(base)
        else if constexpr (std::is_same_v<T, ast::TypeRefine>) {
          return SelfOccursInType(node.base);
        }
        // All other types: SelfOccurs = false
        // TypePrim, TypeString, TypeBytes, TypeDynamic, TypeOpaque
        else {
          return false;
        }
      },
      type.node);
}

// SelfOccurs(m) = SelfOccurs(ReturnType(m)) OR any(SelfOccurs(param.type) for param in m.params)
static bool SelfOccursInMethod(const ast::ClassMethodDecl& method) {
  SPEC_DEF("SelfOccurs", "5.4.1");

  // Check return type
  if (method.return_type_opt && SelfOccursInType(method.return_type_opt)) {
    return true;
  }

  // Check each parameter type
  for (const auto& param : method.params) {
    if (SelfOccursInType(param.type)) {
      return true;
    }
  }

  return false;
}

// ============================================================================
// §6.10 VTable Eligibility
// ============================================================================

static bool HasReceiver(const ast::ClassMethodDecl& method) {
  return std::visit(
      [](const auto& recv) -> bool {
        using R = std::decay_t<decltype(recv)>;
        if constexpr (std::is_same_v<R, ast::ReceiverShorthand>) {
          return true;
        } else {
          return static_cast<bool>(recv.type);
        }
      },
      method.receiver);
}

static bool HasGenericParams(const ast::ClassMethodDecl& method) {
  return method.generic_params.has_value() &&
         !method.generic_params->params.empty();
}

bool IsVTableEligible(const ast::ClassMethodDecl& method) {
  SPEC_DEF("vtable_eligible", "5.4.1");

  // vtable_eligible(m) = HasReceiver(m) AND NOT HasGenericParams(m) AND NOT SelfOccurs(m)
  //
  // Per §5.4.1:
  //   HasReceiver(m) = m.receiver != empty
  //   SelfOccurs(m) = SelfOccurs(ReturnType(m)) OR any param has SelfOccurs(ty)
  //
  // HasReceiver(m) = m.receiver != ⊥
  const bool has_receiver = HasReceiver(method);
  const bool has_generic_params = HasGenericParams(method);

  // NOT SelfOccurs(m) - Self must not occur in params or return type
  const bool self_occurs = SelfOccursInMethod(method);

  return has_receiver && !has_generic_params && !self_occurs;
}

std::vector<std::string> VTableEligible(const ast::ClassDecl& class_decl) {
  SPEC_DEF("VTableEligible", "");

  std::vector<std::string> eligible;

  for (const auto& item : class_decl.items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      if (IsVTableEligible(*method)) {
        eligible.push_back(method->name);
      }
    }
  }

  return eligible;
}

// ============================================================================
// §6.10 DispatchSym - Symbol resolution for vtable slots
// ============================================================================

std::string DispatchSym(const analysis::TypeRef& type,
                        const analysis::TypePath& class_path,
                        const std::string& method_name,
                        const ast::ClassDecl& class_decl,
                        LowerCtx& ctx) {
  analysis::ScopeContext scope;
  if (ctx.sigma) {
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = ctx.module_path;
  }

  // Find the class method declaration
  const auto* class_method = analysis::LookupClassMethod(scope, class_path, method_name);
  if (!class_method) {
    return "";
  }

  const auto stripped = analysis::StripPerm(type);
  const auto lookup = analysis::LookupMethodStatic(scope, stripped, method_name);

  // (DispatchSym-Impl): Type implements the method
  if (lookup.record_method) {
    if (lookup.record_path.empty()) {
      return "";
    }
    SPEC_RULE("DispatchSym-Impl");
    return MangleMethod(lookup.record_path, *lookup.record_method);
  }

  // (DispatchSym-Default-None): No implementation -> use default impl symbol
  if (class_method->body_opt) {
    SPEC_RULE("DispatchSym-Default-None");
    return MangleDefaultImpl(stripped, class_path, class_method->name);
  }

  return "";
}

// ============================================================================
// §6.10 VTable - Vtable generation
// ============================================================================

VTableInfo VTable(const analysis::TypeRef& type,
                  const analysis::TypePath& class_path,
                  const ast::ClassDecl& class_decl,
                  LowerCtx& ctx) {
  SPEC_RULE("VTable-Order");

  VTableInfo info;

  // Generate vtable symbol
  info.symbol = MangleVTable(type, class_path);

  // Get type size and alignment
  // Use layout helpers when available
  analysis::ScopeContext scope;
  if (ctx.sigma) {
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = ctx.module_path;
  }
  const auto size = ::cursive::analysis::layout::SizeOf(scope, type);
  const auto align = ::cursive::analysis::layout::AlignOf(scope, type);
  info.type_size = size.value_or(0);
  info.type_align = align.value_or(1);

  // Get drop glue symbol
  info.drop_sym = DropGlueSym(type, ctx);

  // Get method symbols in vtable order
  auto eligible = VTableEligible(class_decl);
  info.method_syms.reserve(eligible.size());

  for (const auto& method_name : eligible) {
    std::string sym = DispatchSym(type, class_path, method_name, class_decl, ctx);
    info.method_syms.push_back(std::move(sym));
  }

  return info;
}

GlobalVTable EmitVTable(const analysis::TypeRef& type,
                        const analysis::TypePath& class_path,
                        const ast::ClassDecl& class_decl,
                        LowerCtx& ctx) {
  SPEC_DEF("EmitVTable", "");
  SPEC_RULE("EmitVTable-Decl");

  VTableInfo info = VTable(type, class_path, class_decl, ctx);

  GlobalVTable gvt;
  gvt.symbol = info.symbol;
  gvt.header.size = info.type_size;
  gvt.header.align = info.type_align;
  gvt.header.drop_sym = info.drop_sym;
  gvt.slots = std::move(info.method_syms);

  return gvt;
}

// ============================================================================
// §6.10 VSlot - Vtable slot lookup
// ============================================================================

std::optional<std::size_t> VSlot(const ast::ClassDecl& class_decl,
                                  const std::string& method_name) {
  SPEC_RULE("VSlot-Entry");

  // (VSlot-Entry)
  // VTableEligible(Cl) = [m_0,...,m_{k-1}]
  // m_i.name = method.name
  // VSlot(Cl, method) => i

  auto eligible = VTableEligible(class_decl);

  for (std::size_t i = 0; i < eligible.size(); ++i) {
    if (eligible[i] == method_name) {
      return i;
    }
  }

  return std::nullopt;  // Method not found in vtable
}

// ============================================================================
// §6.10 DynPack - Fat pointer creation
// ============================================================================

DynPackResult DynPack(const analysis::TypeRef& type,
                      const ast::Expr& expr,
                      const analysis::TypePath& class_path,
                      const ast::ClassDecl& class_decl,
                      LowerCtx& ctx) {
  SPEC_RULE("Lower-Dynamic-Form");

  // (Lower-Dynamic-Form)
  // IsPlace(e)
  // LowerAddrOf(e) => <IR, addr>
  // T_e = ExprType(e), T = StripPerm(T_e)
  // T <: Cl
  // DynPack(T, e) => <RawPtr(imm, addr), VTable(T, Cl)>

  DynPackResult result;

  // Lower the address of the expression
  LowerResult addr_result = LowerAddrOf(expr, ctx);
  result.ir = addr_result.ir;
  result.data_ptr = addr_result.value;
  result.data_ptr.kind = IRValue::Kind::Opaque;  // Mark as raw pointer

  // Get the vtable for this type implementing the class
  VTableInfo vtable_info = VTable(type, class_path, class_decl, ctx);
  result.vtable_sym = vtable_info.symbol;
  ctx.RegisterRequiredVTable(vtable_info.symbol, type, class_path);

  return result;
}

// ============================================================================
// §6.10 LowerDynCall - Dynamic dispatch call lowering
// ============================================================================

LowerResult LowerDynCall(const IRValue& base_ptr,
                         const std::string& vtable_sym,
                         const ast::ClassDecl& class_decl,
                         const std::string& method_name,
                         const std::vector<IRValue>& args,
                         LowerCtx& ctx) {
  SPEC_RULE("Lower-DynCall");

  // (Lower-DynCall)
  // VSlot(Cl, name) => i
  // LowerDynCall(base, name, args) => SeqIR(CallVTable(base, i, args), PanicCheck)

  auto slot_opt = VSlot(class_decl, method_name);

  if (!slot_opt.has_value()) {
    // Method not found - return error result
    IRValue error_value = ctx.FreshTempValue("dyncall_error");
    return LowerResult{EmptyIR(), error_value};
  }

  std::size_t slot = *slot_opt;

  // Look up the method's return type from the class declaration so the
  // LLVM emitter can generate the correct return type for the indirect call.
  analysis::TypeRef method_ret_type;
  for (const auto& item : class_decl.items) {
    if (const auto* method = std::get_if<ast::ClassMethodDecl>(&item)) {
      if (method->name == method_name && method->return_type_opt && ctx.sigma) {
        analysis::ScopeContext scope;
        scope.sigma = *ctx.sigma;
        scope.sigma_source = ctx.sigma;
        scope.current_module = ctx.module_path;
        if (auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, method->return_type_opt)) {
          method_ret_type = *lowered;
        }
        break;
      }
    }
  }

  // Create CallVTable IR
  IRCallVTable call;
  call.base = base_ptr;
  call.slot = slot;
  call.args = args;
  call.ret_type = method_ret_type;

  // Result value
  IRValue result_value = ctx.FreshTempValue("dyncall_result");
  call.result = result_value;

  // Register the return type so downstream lowering can look it up.
  if (method_ret_type) {
    ctx.RegisterValueType(result_value, method_ret_type);
  }

  // Sequence: CallVTable + PanicCheck
  std::vector<IRPtr> parts;
  parts.push_back(MakeIR(std::move(call)));
  parts.push_back(PanicCheck(ctx));

  return LowerResult{SeqIR(std::move(parts)), result_value};
}

// ============================================================================
// §6.10 Dynamic Object ::cursive::analysis::layout::Layout
// ============================================================================

DynObjectLayout GetDynObjectLayout() {
  SPEC_DEF("DynObjectLayout", "");

  // A dynamic object (trait object) on Win64 x86_64-pc-windows-msvc:
  // - data_ptr: 8 bytes at offset 0
  // - vtable_ptr: 8 bytes at offset 8
  // Total size: 16 bytes
  // Alignment: 8 bytes (pointer alignment)

  DynObjectLayout layout;
  layout.size = 16;
  layout.align = 8;
  layout.data_offset = 0;
  layout.vtable_offset = 8;

  return layout;
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorDynDispatchRules() {
  // §6.10 Dynamic Dispatch
  SPEC_RULE("DispatchSym-Impl");
  SPEC_RULE("DispatchSym-Default-None");
  SPEC_RULE("DispatchSym-Default-Mismatch");
  SPEC_RULE("VTable-Order");
  SPEC_RULE("VSlot-Entry");
  SPEC_RULE("Lower-Dynamic-Form");
  SPEC_RULE("Lower-DynCall");

  // Definitions
  SPEC_DEF("VTableEligible", "");
  SPEC_DEF("vtable_eligible", "");
  SPEC_DEF("EmitVTable", "");
  SPEC_DEF("DynObjectLayout", "");
}

}  // namespace cursive::codegen

