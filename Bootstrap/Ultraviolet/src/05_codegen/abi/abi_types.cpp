// =============================================================================
// ABI Type Lowering Dispatcher (§6.2.2)
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md
//   - Section 6.2.2 ABI Type Lowering
//   - ABITy judgment: maps semantic types to ABI types
//
// This file dispatches to individual type-specific implementations in types/.
// Each type category has its own file following the layout/ pattern.
//
// INDIVIDUAL TYPE FILES:
//   - types/abi_type_prim.cpp   - primitives (ABI-Prim)
//   - types/abi_type_ptr.cpp    - safe pointers (ABI-Ptr)
//   - types/abi_type_rawptr.cpp - raw pointers (ABI-RawPtr)
//   - types/abi_type_func.cpp   - function and closure types (ABI-Func)
//   - types/abi_type_slice.cpp  - slices (ABI-Slice)
//   - types/abi_type_dynamic.cpp- dynamic objects (ABI-Dynamic)
//   - types/abi_type_range.cpp  - ranges (ABI-Range)
//   - types/abi_type_tuple.cpp  - tuples (ABI-Tuple)
//   - types/abi_type_array.cpp  - arrays (ABI-Array)
//   - types/abi_type_union.cpp  - unions (ABI-Union)
//   - types/abi_type_string.cpp - strings/bytes (ABI-StringBytes)
//   - types/abi_type_modal.cpp  - modal states (ABI-Modal)
//   - types/abi_type_path.cpp   - path types (ABI-Record, ABI-Enum, ABI-Alias)
//
// =============================================================================

#include "05_codegen/abi/abi.h"
#include "04_analysis/layout/layout.h"
#include "00_core/spec_trace.h"

namespace ultraviolet::codegen {

// Forward declarations for type-specific handlers.
std::optional<ABIType> ABITyPrim(const analysis::ScopeContext& ctx,
                                 const analysis::TypePrim& prim);
std::optional<ABIType> ABITyPtr(const analysis::ScopeContext& ctx,
                                const analysis::TypePtr& ptr);
std::optional<ABIType> ABITyRawPtr(const analysis::ScopeContext& ctx,
                                   const analysis::TypeRawPtr& rawptr);
std::optional<ABIType> ABITyFunc(const analysis::ScopeContext& ctx,
                                 const analysis::TypeFunc& func);
std::optional<ABIType> ABITySlice(const analysis::ScopeContext& ctx,
                                  const analysis::TypeSlice& slice);
std::optional<ABIType> ABITyDynamic(const analysis::ScopeContext& ctx,
                                    const analysis::TypeDynamic& dyn);
std::optional<ABIType> ABITyRange(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type);
std::optional<ABIType> ABITyTuple(const analysis::ScopeContext& ctx,
                                  const analysis::TypeTuple& tuple);
std::optional<ABIType> ABITyArray(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type);
std::optional<ABIType> ABITyUnion(const analysis::ScopeContext& ctx,
                                  const analysis::TypeUnion& uni);
std::optional<ABIType> ABITyString(const analysis::ScopeContext& ctx,
                                   const analysis::TypeRef& type);
std::optional<ABIType> ABITyBytes(const analysis::ScopeContext& ctx,
                                  const analysis::TypeRef& type);
std::optional<ABIType> ABITyModalState(const analysis::ScopeContext& ctx,
                                       const analysis::TypeRef& type);
std::optional<ABIType> ABITyPathType(const analysis::ScopeContext& ctx,
                                     const analysis::TypeRef& type,
                                     const analysis::TypePathType& path_type);

std::optional<ABIType> ABITy(const analysis::ScopeContext& ctx,
                             const analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }

  // (ABI-Perm)
  // Strip permission wrapper - ABI type is the same as the base type.
  if (const auto* perm = std::get_if<analysis::TypePerm>(&type->node)) {
    SPEC_RULE("ABI-Perm");
    return ABITy(ctx, perm->base);
  }

  // (ABI-Prim)
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return ABITyPrim(ctx, *prim);
  }

  // (ABI-Ptr)
  if (const auto* ptr = std::get_if<analysis::TypePtr>(&type->node)) {
    return ABITyPtr(ctx, *ptr);
  }

  // (ABI-RawPtr)
  if (const auto* rawptr = std::get_if<analysis::TypeRawPtr>(&type->node)) {
    return ABITyRawPtr(ctx, *rawptr);
  }

  // (ABI-Func)
  if (const auto* func = std::get_if<analysis::TypeFunc>(&type->node)) {
    return ABITyFunc(ctx, *func);
  }
  if (std::holds_alternative<analysis::TypeClosure>(type->node)) {
    SPEC_RULE("ABI-Func");
    return ABIType{2 * ::ultraviolet::analysis::layout::PtrSize(ctx),
                   ::ultraviolet::analysis::layout::PtrAlign(ctx)};
  }

  // (ABI-Slice)
  if (const auto* slice = std::get_if<analysis::TypeSlice>(&type->node)) {
    return ABITySlice(ctx, *slice);
  }

  // (ABI-Dynamic)
  if (const auto* dyn = std::get_if<analysis::TypeDynamic>(&type->node)) {
    return ABITyDynamic(ctx, *dyn);
  }

  // (ABI-Range)
  if (analysis::IsRangeType(type)) {
    return ABITyRange(ctx, type);
  }

  // (ABI-Tuple)
  if (const auto* tuple = std::get_if<analysis::TypeTuple>(&type->node)) {
    return ABITyTuple(ctx, *tuple);
  }

  // (ABI-Array)
  if (std::holds_alternative<analysis::TypeArray>(type->node)) {
    return ABITyArray(ctx, type);
  }

  // (ABI-Union)
  if (const auto* uni = std::get_if<analysis::TypeUnion>(&type->node)) {
    return ABITyUnion(ctx, *uni);
  }

  // (ABI-StringBytes) - String
  if (std::holds_alternative<analysis::TypeString>(type->node)) {
    return ABITyString(ctx, type);
  }

  // (ABI-StringBytes) - Bytes
  if (std::holds_alternative<analysis::TypeBytes>(type->node)) {
    return ABITyBytes(ctx, type);
  }

  // (ABI-Modal) - TypeModalState
  if (std::holds_alternative<analysis::TypeModalState>(type->node)) {
    return ABITyModalState(ctx, type);
  }

  // Handle TypePathType - may be Record, Enum, Modal, or Alias
  if (const auto* path_type = std::get_if<analysis::TypePathType>(&type->node)) {
    return ABITyPathType(ctx, type, *path_type);
  }

  return std::nullopt;
}

}  // namespace ultraviolet::codegen
