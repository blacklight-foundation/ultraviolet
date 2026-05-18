#pragma once

// =============================================================================
// VTable Emission
// =============================================================================
//
// This file provides LLVM-specific VTable emission functions.
// VTables are the runtime data structures that enable dynamic dispatch for
// class method calls.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.10 VTable Emission (lines 17597-17645)
//   - Mangle-VTable rule (lines 15517-15520)
//   - Linkage-VTable rule (lines 15620-15623)
//   - DynLayout rule (lines 17556-17558)
//
// VTABLE LAYOUT:
//   {
//     usize size,    // sizeof(T)
//     usize align,   // alignof(T)
//     ptr drop,      // drop glue function pointer
//     ptr slot_0,    // method slot 0
//     ptr slot_1,    // method slot 1
//     ...
//   }
//
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

#include "01_project/target_profile.h"
#include "05_codegen/dyn_dispatch/dyn_dispatch.h"
#include "05_codegen/ir/ir_model.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// =============================================================================
// VTable Header ::ultraviolet::analysis::layout::Layout
// =============================================================================

/// ::ultraviolet::analysis::layout::Layout information for VTable header fields.
struct VTableHeaderLayout {
  /// Offset of the size field (bytes).
  std::uint64_t size_offset = 0;

  /// Offset of the alignment field (bytes).
  std::uint64_t align_offset = 8;

  /// Offset of the drop glue pointer (bytes).
  std::uint64_t drop_offset = 16;

  /// Offset of the first slot pointer (bytes).
  std::uint64_t slots_offset = 24;

  /// Size of each slot (pointer size).
  std::uint64_t slot_size = 8;
};

/// Get the VTable header layout for a target profile.
VTableHeaderLayout GetVTableHeaderLayout(
    project::TargetProfile target_profile);

/// Get the VTable header layout for the current lowering target.
VTableHeaderLayout GetVTableHeaderLayout(const LowerCtx& ctx);

// =============================================================================
// VTable Symbol Resolution
// =============================================================================

/// Resolve a symbol name to a function or global value.
/// Returns the mangled symbol name ready for LLVM lookup.
std::string ResolveVTableSlotSymbol(
    const analysis::TypeRef& impl_type,
    const analysis::TypePath& class_path,
    const std::string& method_name,
    LowerCtx& ctx);

/// Get the drop glue symbol for a type.
std::string GetDropGlueSymbol(const analysis::TypeRef& type);

// =============================================================================
// VTable IR Generation
// =============================================================================

/// Generate the GlobalVTable IR for a type implementing a class.
///
/// This creates the IR representation of the vtable. The actual LLVM
/// emission happens later in the codegen pipeline.
///
/// Parameters:
///   - type: The concrete type implementing the class
///   - class_path: Path to the class declaration
///   - class_decl: The class declaration AST
///   - ctx: Lowering context
///
/// Returns:
///   GlobalVTable IR declaration.
GlobalVTable GenerateVTableIR(
    const analysis::TypeRef& type,
    const analysis::TypePath& class_path,
    const ast::ClassDecl& class_decl,
    LowerCtx& ctx);

// =============================================================================
// VTable Validation
// =============================================================================

/// Validate that a VTable has all required slots filled.
///
/// Returns true if the vtable is valid, false otherwise.
/// If invalid, populates the error_messages vector with details.
bool ValidateVTable(
    const GlobalVTable& vtable,
    std::vector<std::string>& error_messages);

/// Check if a symbol is a valid vtable slot target.
///
/// A valid slot target is:
///   - A function with the correct signature
///   - Not a generic function (in C0, all functions are monomorphic)
bool IsValidSlotTarget(const std::string& symbol, LowerCtx& ctx);

// =============================================================================
// VTable References
// =============================================================================

/// Collect all VTable references from an IR module.
///
/// This is used during LLVM emission to ensure all referenced
/// vtables are emitted before they're used.
std::vector<std::string> CollectVTableRefs(const IRDecls& decls);
std::vector<std::string> CollectVTableRefs(const IRDecls& decls,
                                           const LowerCtx& ctx);
std::vector<std::string> CollectVTableRefs(const LowerCtx& ctx);

/// Check if a symbol is a vtable symbol.
bool IsVTableSymbol(const std::string& symbol);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

/// Emit SPEC_RULE anchors for VTable emission rules.
void AnchorVTableEmitRules();

}  // namespace ultraviolet::codegen
