// =============================================================================
// File: 05_codegen/llvm/llvm_types.h
// Construct: Cursive Type to LLVM Type Mapping
// Spec Section: 6.12.2, 6.12.8
// Spec Rules: LLVMTy-*, OpaquePointerModel, LLVMPrim, LLVMStruct, LLVMArray
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"

// Forward declarations for LLVM types
namespace llvm {
class ArrayType;
class LLVMContext;
class PointerType;
class StructType;
class Type;
}  // namespace llvm

namespace cursive::codegen {

// Forward declarations
class LLVMEmitter;
struct LowerCtx;

// =============================================================================
// §6.12.2 Opaque Pointer Model (LLVM 21)
// =============================================================================

// AddrSpace(T) - Address space for pointer types
// AddrSpace(T) = 0 for all Cursive pointer types
constexpr unsigned kDefaultAddressSpace = 0;

// LLVMPtrTy(T) = ptr addrspace(AddrSpace(T))
// All pointers in Cursive use opaque pointers in LLVM 21
llvm::PointerType* GetOpaquePointerType(llvm::LLVMContext& context,
                                        unsigned address_space = kDefaultAddressSpace);

// =============================================================================
// §6.12.8 LLVM Type Mapping
// =============================================================================

// LLVMZST = {} (zero-sized type, empty struct)
llvm::StructType* GetZSTType(llvm::LLVMContext& context);

// -----------------------------------------------------------------------------
// Primitive Type Mapping
// -----------------------------------------------------------------------------

// LLVMPrim(name) - Maps Cursive primitive type names to LLVM types
// i8, u8 → i8
// i16, u16 → i16
// i32, u32 → i32
// i64, u64 → i64
// i128, u128 → i128
// usize, isize → i64 (on x86_64)
// f16 → half
// f32 → float
// f64 → double
// bool → i8 (not i1, for ABI compatibility)
// char → i32 (Unicode scalar value)
// (), ! → LLVMZST
llvm::Type* GetPrimType(llvm::LLVMContext& context, std::string_view name);

// Check if a primitive type name is valid
bool IsValidPrimName(std::string_view name);

// -----------------------------------------------------------------------------
// Aggregate Type Construction
// -----------------------------------------------------------------------------

// LLVMStruct([t_1, ..., t_k]) = { t_1, ..., t_k }
llvm::StructType* CreateStructType(llvm::LLVMContext& context,
                                   const std::vector<llvm::Type*>& elements,
                                   bool is_packed = false);

// LLVMArray(n, t) = [n x t]
llvm::ArrayType* CreateArrayType(llvm::Type* element_type, std::uint64_t count);

// Pad(n) - Create padding type for struct layout
// Pad(0) = [] (no padding)
// Pad(n) = [n x i8] for n > 0
llvm::Type* CreatePaddingType(llvm::LLVMContext& context, std::uint64_t bytes);

// -----------------------------------------------------------------------------
// ::cursive::analysis::layout::Layout-Aware Struct Construction
// -----------------------------------------------------------------------------

// StructElems(fields, offsets, size) - Build struct element list with padding
// Computes proper padding between fields to match layout
std::vector<llvm::Type*> ComputeStructElements(
    LLVMEmitter& emitter,
    const std::vector<analysis::TypeRef>& fields,
    const std::vector<std::uint64_t>& offsets,
    std::uint64_t total_size,
    std::uint64_t required_align = 1);

// TaggedElems(disc, payload_size, payload_align, size) - Build tagged union elements
// ::cursive::analysis::layout::Layout: [disc_type, padding, [payload_size x i8], tail_padding]
std::vector<llvm::Type*> ComputeTaggedElements(
    LLVMEmitter& emitter,
    const analysis::TypeRef& disc_type,
    std::uint64_t payload_size,
    std::uint64_t payload_align,
    std::uint64_t total_size);

// -----------------------------------------------------------------------------
// Composite Type Helpers
// -----------------------------------------------------------------------------

// SlicePtrTy(T) = LLVMPtrTy(TypeRawPtr(imm, T))
// Slice representation: { ptr, usize }
llvm::StructType* GetSliceType(llvm::LLVMContext& context);

// String/Bytes view representation: { ptr, usize }
llvm::StructType* GetStringViewType(llvm::LLVMContext& context);
llvm::StructType* GetBytesViewType(llvm::LLVMContext& context);

// String/Bytes managed representation: { ptr, len, capacity }
llvm::StructType* GetStringManagedType(llvm::LLVMContext& context);
llvm::StructType* GetBytesManagedType(llvm::LLVMContext& context);

// Runtime dispatch range representation: { kind: u8, start: usize, end: usize }.
// Language range types use constructor-specific struct shapes and are lowered via
// LLVMEmitter::GetLLVMType.
llvm::StructType* GetRangeType(llvm::LLVMContext& context);

// Dynamic type representation: { data: ptr, vtable: ptr }
llvm::StructType* GetDynamicType(llvm::LLVMContext& context);

// -----------------------------------------------------------------------------
// Type Size and Alignment
// -----------------------------------------------------------------------------

// Get the size of an LLVM type in bytes
std::uint64_t GetTypeSize(llvm::Type* type);

// Get the alignment of an LLVM type in bytes
std::uint64_t GetTypeAlignment(llvm::Type* type);

// Get an integer type that can hold a value of given size
// Used for ABI-compatible passing of small aggregates
llvm::Type* GetIntTypeForSize(llvm::LLVMContext& context, std::uint64_t size);

// Get alignment marker type for struct trailing alignment
// Returns nullptr if align is not a standard alignment (1, 2, 4, 8, 16)
llvm::Type* GetAlignmentMarkerType(llvm::LLVMContext& context, std::uint64_t align);

// -----------------------------------------------------------------------------
// Tagged Type Helpers (for enums, unions, modals)
// -----------------------------------------------------------------------------

// Create a blob type for untyped payload storage with alignment marker
// { [size x i8], [0 x align_marker] }
llvm::StructType* CreateTaggedBlobType(llvm::LLVMContext& context,
                                       std::uint64_t size,
                                       std::uint64_t align);

// Create an ABI-compatible type for small tagged types
// Uses integer types when possible for better ABI handling
llvm::Type* CreateTaggedABIType(llvm::LLVMContext& context,
                                std::uint64_t size,
                                std::uint64_t align);

// Create the LLVM struct type for a tagged discriminated type
// { disc_type, padding, [payload_size x i8], tail_padding }
llvm::StructType* CreateTaggedStructType(LLVMEmitter& emitter,
                                         const analysis::TypeRef& disc_type,
                                         std::uint64_t payload_size,
                                         std::uint64_t payload_align,
                                         std::uint64_t total_size);

// -----------------------------------------------------------------------------
// Async Type ::cursive::analysis::layout::Layout (§5.4.5)
// -----------------------------------------------------------------------------

// Build LLVM type for Async<Out, In, Result, E>
// ::cursive::analysis::layout::Layout matches ModalLayout with tagged discriminant + max payload blob
llvm::Type* BuildAsyncLLVMType(LLVMEmitter& emitter,
                               const std::vector<analysis::TypeRef>& generic_args);

}  // namespace cursive::codegen
