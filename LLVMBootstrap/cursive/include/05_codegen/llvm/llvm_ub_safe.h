// =============================================================================
// File: 05_codegen/llvm/llvm_ub_safe.h
// Construct: UB-Safe Arithmetic Operations
// Spec Section: 6.12.4, 6.12.5
// Spec Rules: LLVMUBSafe, CheckedOverflow, CheckedDivRem, CheckedShifts,
//             FrozenPoisonUses, MemIntrinsics-*
// =============================================================================
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Forward declarations for LLVM types
namespace llvm {
class IRBuilderBase;
class Module;
class Type;
class Value;
}  // namespace llvm

namespace cursive::codegen {

// Forward declarations
class LLVMEmitter;

// =============================================================================
// §6.12.4 UB and Poison Avoidance (LLVM 21)
// =============================================================================

// LLVMUBSafe(LLVMIR) ⟺
//   NoUndefPoison(LLVMIR) ∧
//   CheckedOverflow(LLVMIR) ∧
//   CheckedDivRem(LLVMIR) ∧
//   CheckedShifts(LLVMIR) ∧
//   FrozenPoisonUses(LLVMIR) ∧
//   InboundsGEP(LLVMIR) ∧
//   NoNSWNUW(LLVMIR)
bool UsesOpcode(const llvm::Module& module, std::string_view op);
bool UsesIntrinsic(const llvm::Module& module, std::string_view name);
bool NoUndefPoison(const llvm::Module& module);
bool CheckedOverflow(const llvm::Module& module);
bool CheckedDivRem(const llvm::Module& module);
bool CheckedShifts(const llvm::Module& module);
bool FrozenPoisonUses(const llvm::Module& module);
bool InboundsGEP(const llvm::Module& module);
bool LLVMUBSafe(const llvm::Module& module);
bool NoNSWNUW(const llvm::Module& module);

// -----------------------------------------------------------------------------
// Checked Arithmetic (T-LLVM-004)
// -----------------------------------------------------------------------------

// CheckedOverflow - Use llvm.*.with.overflow intrinsics instead of plain add/sub/mul
// These intrinsics return {result, overflow_flag} and we check the flag

// (LLVMUBSafe-Add)
// Emit checked addition using llvm.sadd.with.overflow / llvm.uadd.with.overflow
// Panics on overflow instead of producing undefined behavior
llvm::Value* EmitCheckedAdd(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// (LLVMUBSafe-Sub)
// Emit checked subtraction using llvm.ssub.with.overflow / llvm.usub.with.overflow
// Panics on overflow instead of producing undefined behavior
llvm::Value* EmitCheckedSub(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// (LLVMUBSafe-Mul)
// Emit checked multiplication using llvm.smul.with.overflow / llvm.umul.with.overflow
// Panics on overflow instead of producing undefined behavior
llvm::Value* EmitCheckedMul(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// CheckedDivRem - Division and remainder with frozen results

// (LLVMUBSafe-Div)
// Emit division with frozen result to avoid poison on division by zero
// For floating-point, uses fdiv directly (IEEE semantics)
// For integers, uses sdiv/udiv with freeze
llvm::Value* EmitCheckedDiv(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// (LLVMUBSafe-Rem)
// Emit remainder with frozen result to avoid poison on division by zero
// For floating-point, uses frem directly (IEEE semantics)
// For integers, uses srem/urem with freeze
llvm::Value* EmitCheckedRem(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// CheckedShifts - Shift operations with frozen results

// (LLVMUBSafe-Shl)
// Emit left shift with frozen result
// Coerces shift amount to match value type
llvm::Value* EmitCheckedShl(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs);

// (LLVMUBSafe-Shr)
// Emit right shift with frozen result
// Uses ashr for signed, lshr for unsigned
// Coerces shift amount to match value type
llvm::Value* EmitCheckedShr(LLVMEmitter& emitter,
                            llvm::Value* lhs,
                            llvm::Value* rhs,
                            bool is_signed);

// -----------------------------------------------------------------------------
// Integer Type Coercion
// -----------------------------------------------------------------------------

// Coerce an integer value to a target integer type
// Uses zero-extend or sign-extend as appropriate
llvm::Value* CoerceInteger(llvm::IRBuilderBase* builder,
                           llvm::Value* value,
                           llvm::Type* target,
                           bool is_unsigned);

// =============================================================================
// §6.12.5 Memory Intrinsics (T-LLVM-005)
// =============================================================================

// MemIntrinsics - Use LLVM memory intrinsics for bulk operations

// (MemIntrinsics-Copy)
// Emit memory copy using llvm.memmove (not memcpy, overlap unknown per spec)
void EmitMemCpy(LLVMEmitter& emitter,
                llvm::Value* dst,
                llvm::Value* src,
                llvm::Value* size,
                std::uint64_t align = 1);

// MemcpyOverlapUnknown(dst, src, n) predicate
bool MemcpyOverlapUnknown(const llvm::Value* dst,
                          const llvm::Value* src,
                          const llvm::Value* size);

// MemcpyAllowed(dst, src, n) predicate
bool MemcpyAllowed(const llvm::Value* dst,
                   const llvm::Value* src,
                   const llvm::Value* size);

// (MemIntrinsics-Set)
// Emit memory set using llvm.memset
void EmitMemSet(LLVMEmitter& emitter,
                llvm::Value* dst,
                llvm::Value* val,
                llvm::Value* size,
                std::uint64_t align = 1);

// (MemIntrinsics-Move)
// Emit memory move using llvm.memmove (handles overlapping regions)
void EmitMemMove(LLVMEmitter& emitter,
                 llvm::Value* dst,
                 llvm::Value* src,
                 llvm::Value* size,
                 std::uint64_t align = 1);

// AggMemcpy(dst, src, n) =
//   Memcpy(dst, src, n)  if MemcpyAllowed(dst, src, n)
//   Memmove(dst, src, n) otherwise
void EmitAggMemcpy(LLVMEmitter& emitter,
                   llvm::Value* dst,
                   llvm::Value* src,
                   llvm::Value* size,
                   std::uint64_t align = 1);

// -----------------------------------------------------------------------------
// Pointer Arithmetic Helpers
// -----------------------------------------------------------------------------

// Emit a byte-offset GEP (getelementptr with i8 element type)
// This avoids inbounds GEP issues by using explicit byte offsets
llvm::Value* EmitByteGEP(LLVMEmitter& emitter,
                         llvm::IRBuilderBase* builder,
                         llvm::Value* base_ptr,
                         std::uint64_t offset);

// Emit a store at a byte offset from a base pointer
void EmitStoreAtOffset(LLVMEmitter& emitter,
                       llvm::IRBuilderBase* builder,
                       llvm::Value* base_ptr,
                       std::uint64_t offset,
                       llvm::Value* value);

// Emit a load at a byte offset from a base pointer
llvm::Value* EmitLoadAtOffset(LLVMEmitter& emitter,
                              llvm::IRBuilderBase* builder,
                              llvm::Value* base_ptr,
                              std::uint64_t offset,
                              llvm::Type* load_type);

// -----------------------------------------------------------------------------
// Poison and Freeze
// -----------------------------------------------------------------------------

// Freeze a value to convert poison to an unspecified but fixed value
// FrozenPoisonUses - All potentially-poison values must be frozen before use
llvm::Value* EmitFreeze(llvm::IRBuilderBase* builder, llvm::Value* value);

// Check if a value might be poison (conservative approximation)
bool MightBePoison(llvm::Value* value);

}  // namespace cursive::codegen
