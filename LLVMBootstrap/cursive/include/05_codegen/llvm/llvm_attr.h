// =============================================================================
// File: 05_codegen/llvm/llvm_attr.h
// Construct: LLVM Attribute Mapping
// Spec Section: 6.12.3
// Spec Rules: LLVM-PtrAttrs-Valid, LLVM-PtrAttrs-Other, LLVM-PtrAttrs-RawPtr,
//             LLVM-ArgAttrs-Ptr, LLVM-ArgAttrs-RawPtr, LLVM-ArgAttrs-NonPtr
// =============================================================================
#pragma once

#include <optional>
#include <vector>

#include "04_analysis/typing/types.h"

// Forward declarations for LLVM types
namespace llvm {
  class AttrBuilder;
  class Function;
  class LLVMContext;
  class Type;
}  // namespace llvm

namespace cursive::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// =============================================================================
// §6.12.3 LLVM Attribute Mapping (Permissions and Pointer State)
// =============================================================================

// LLVMAttrJudg = {PtrStateOf(T) = s, LLVMPtrAttrs(T) ⇓ A, LLVMArgAttrs(T) ⇓ A}

// -----------------------------------------------------------------------------
// Permission and Pointer State Extraction
// -----------------------------------------------------------------------------

// StripPerm(T) - Removes permission wrapper from type
// StripPerm(TypePerm(p, T)) = StripPerm(T)
// StripPerm(T) = T otherwise
analysis::TypeRef StripPerm(const analysis::TypeRef& type);

// PermOf(T) - Extracts the permission from a type
// Returns Permission::Shared if no explicit permission wrapper
analysis::Permission PermOf(const analysis::TypeRef& type);

// PtrStateOf(T) - Extracts pointer state from type
// (PtrStateOf-Perm): PtrStateOf(TypePerm(p, T)) = PtrStateOf(T)
// Returns the PtrState if the underlying type is a Ptr, nullopt otherwise
std::optional<analysis::PtrState> PtrStateOf(const analysis::TypeRef& type);

// -----------------------------------------------------------------------------
// Attribute Building
// -----------------------------------------------------------------------------

// Attribute set representation
enum class AttrKind {
  NonNull,
  NoUndef,
  NoAlias,
  ReadOnly,
  Dereferenceable,
  Alignment,
  StructRet,
  NoCapture,
  NoReturn,
  NoUnwind,
};

struct AttrSpec {
  AttrKind kind;
  std::uint64_t value = 0;  // For Dereferenceable/Alignment
  llvm::Type* type = nullptr;  // For typed attributes like sret
};

using AttrSet = std::vector<AttrSpec>;

// (LLVM-PtrAttrs-Valid)
// StripPerm(T) = TypePtr(U, `Valid`)
// ⇒ LLVMPtrAttrs(T) ⇓ {nonnull, dereferenceable(sizeof(U)), align(alignof(U)), noundef}
//
// (LLVM-PtrAttrs-Other)
// StripPerm(T) = TypePtr(U, s) where s ∈ {⊥, Null, Expired}
// ⇒ LLVMPtrAttrs(T) ⇓ ∅
//
// (LLVM-PtrAttrs-RawPtr)
// StripPerm(T) = TypeRawPtr(q, U)
// ⇒ LLVMPtrAttrs(T) ⇓ ∅
AttrSet ComputePtrAttrs(const analysis::TypeRef& type,
                        const LowerCtx* ctx);

// (LLVM-ArgAttrs-Ptr)
// LLVMArgAttrsPtr(T) = (PermOf(T) = unique → {noalias}) ∪ (PermOf(T) = const → {readonly})
// StripPerm(T) ∈ {TypePtr, TypeFunc}
// ⇒ LLVMArgAttrs(T) ⇓ LLVMArgAttrsPtr(T)
//
// (LLVM-ArgAttrs-RawPtr)
// StripPerm(T) = TypeRawPtr(_, _)
// ⇒ LLVMArgAttrs(T) ⇓ ∅
//
// (LLVM-ArgAttrs-NonPtr)
// StripPerm(T) ∉ {TypePtr, TypeRawPtr, TypeFunc}
// ⇒ LLVMArgAttrs(T) ⇓ ∅
AttrSet ComputeArgAttrs(const analysis::TypeRef& type);

// LLVMArgAttrsExt(x, T) = LLVMArgAttrs(T) ∪ OptArgAttrs(x)
// OptArgAttrs(x) ⊆ {nocapture} ∧ (nocapture ∈ OptArgAttrs(x) ⇒ NoEscapeParam(x))
bool NoEscapeParam(std::string_view param_name);
AttrSet ComputeArgAttrsExt(const std::string& param_name,
                           const analysis::TypeRef& type);

// -----------------------------------------------------------------------------
// LLVM AttrBuilder Integration
// -----------------------------------------------------------------------------

// Adds an AttrSet to an LLVM AttrBuilder.
void AddAttrSetToBuilder(llvm::AttrBuilder& builder,
                         const AttrSet& attrs);

// Adds pointer attributes to an LLVM AttrBuilder based on type
// Computes both PtrAttrs and ArgAttrs and applies them
void AddArgAttrsToBuilder(llvm::AttrBuilder& builder,
                          const analysis::TypeRef& type);

// Adds pointer-specific attributes (nonnull, dereferenceable, etc.)
void AddPtrAttrsToBuilder(llvm::AttrBuilder& builder,
                          const analysis::TypeRef& type,
                          const LowerCtx* ctx);

// -----------------------------------------------------------------------------
// Function Attribute Management
// -----------------------------------------------------------------------------

// DeclAttrsOk(sym) ⟺
//   (sym = PanicSym ⇒ {noreturn, nounwind} ⊆ DeclAttrs(sym)) ∧
//   (sym ≠ PanicSym ⇒ nounwind ∈ DeclAttrs(sym))
AttrSet DeclAttrs(std::string_view symbol);

// Check if declaration attributes are valid per spec
bool DeclAttrsOk(std::string_view symbol, const AttrSet& attrs);

}  // namespace cursive::codegen
