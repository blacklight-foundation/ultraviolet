#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "05_codegen/ir/ir_model.h"
#include "05_codegen/lower/lower_expr.h"
#include "05_codegen/cleanup/cleanup.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::codegen {

// ============================================================================
// §6.10 Dynamic Dispatch
// ============================================================================
// DynDispatchJudg = {VTable, VSlot, DynPack, LowerDynCall}

// ============================================================================
// §6.10 VTable Eligibility
// ============================================================================

// VTableEligible(Cl) = [m ∈ EffMethods(Cl) | vtable_eligible(m)]
// Returns the list of methods that are eligible for vtable dispatch
std::vector<std::string> VTableEligible(const ast::ClassDecl& class_decl);

// Check if a method is vtable-eligible
// A method is vtable-eligible if it:
// - Has a receiver (is not a static method)
// - Is not generic (in Ultraviolet, no generics so all receiver methods qualify)
bool IsVTableEligible(const ast::ClassMethodDecl& method);

// ============================================================================
// §6.10 DispatchSym - Symbol resolution for vtable slots
// ============================================================================

// (DispatchSym-Impl): Type implements method → use type's method symbol
// (DispatchSym-Default-None): No implementation → use default impl symbol
// (DispatchSym-Default-Mismatch): Signature mismatch → use default impl symbol
std::string DispatchSym(const analysis::TypeRef& type,
                        const analysis::TypePath& class_path,
                        const std::string& method_name,
                        const ast::ClassDecl& class_decl,
                        LowerCtx& ctx);

// ============================================================================
// §6.10 VTable - Vtable generation
// ============================================================================

// VTableInfo holds the vtable for a type implementing a class
struct VTableInfo {
  std::string symbol;                    // Mangled vtable symbol
  std::uint64_t type_size;               // sizeof(T)
  std::uint64_t type_align;              // alignof(T)
  std::string drop_sym;                  // Drop glue symbol
  std::vector<std::string> method_syms;  // Slot symbols in order
};

// (VTable-Order)
// VTableEligible(Cl) = [m_1,...,m_k]
// ∀i, DispatchSym(T, Cl, m_i.name) = sym_i
// VTable(T, Cl) ⇓ [sym_1,...,sym_k]
VTableInfo VTable(const analysis::TypeRef& type,
                  const analysis::TypePath& class_path,
                  const ast::ClassDecl& class_decl,
                  LowerCtx& ctx);

// Generate the vtable IR declaration
GlobalVTable EmitVTable(const analysis::TypeRef& type,
                        const analysis::TypePath& class_path,
                        const ast::ClassDecl& class_decl,
                        LowerCtx& ctx);

// ============================================================================
// §6.10 VSlot - Vtable slot lookup
// ============================================================================

// (VSlot-Entry)
// VTableEligible(Cl) = [m_0,...,m_{k-1}]
// m_i.name = method.name
// VSlot(Cl, method) ⇓ i
std::optional<std::size_t> VSlot(const ast::ClassDecl& class_decl,
                                  const std::string& method_name);

// ============================================================================
// §6.10 DynPack - Fat pointer creation
// ============================================================================

// DynPackResult holds the result of packing a value for dynamic dispatch
struct DynPackResult {
  IRPtr ir;                              // Setup IR
  IRValue data_ptr;                      // RawPtr(imm, addr)
  std::string vtable_sym;                // VTable symbol
};

// (Lower-Dynamic-Form)
// IsPlace(e)
// LowerAddrOf(e) ⇓ ⟨IR, addr⟩
// T_e = ExprType(e), T = StripPerm(T_e)
// T <: Cl
// DynPack(T, e) ⇓ ⟨RawPtr(imm, addr), VTable(T, Cl)⟩
DynPackResult DynPack(const analysis::TypeRef& type,
                      const ast::Expr& expr,
                      const analysis::TypePath& class_path,
                      const ast::ClassDecl& class_decl,
                      LowerCtx& ctx);

// ============================================================================
// §6.10 LowerDynCall - Dynamic dispatch call lowering
// ============================================================================

// (Lower-DynCall)
// VSlot(Cl, name) ⇓ i
// LowerDynCall(base, name, args) ⇓ SeqIR(CallVTable(base, i, args), PanicCheck)
LowerResult LowerDynCall(const IRValue& base_ptr,
                         const std::string& vtable_sym,
                         const ast::ClassDecl& class_decl,
                         const std::string& method_name,
                         const std::vector<IRValue>& args,
                         LowerCtx& ctx);

// ============================================================================
// §6.10 Dynamic Object ::ultraviolet::analysis::layout::Layout
// ============================================================================

// A dynamic object (trait object) consists of:
// - A data pointer (ptr to the concrete value)
// - A vtable pointer (ptr to the type's vtable for the class)

struct DynObjectLayout {
  std::uint64_t size;   // Total size (2 pointers)
  std::uint64_t align;  // Alignment (pointer alignment)
  std::uint64_t data_offset;   // Offset of data pointer
  std::uint64_t vtable_offset; // Offset of vtable pointer
};

// Get the layout of a dynamic object
DynObjectLayout GetDynObjectLayout();

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorDynDispatchRules();

}  // namespace ultraviolet::codegen
