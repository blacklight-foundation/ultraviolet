// =============================================================================
// Intrinsic Declarations Implementation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Contract intrinsics @result, @entry (lines 3357, 23204-23266)
//   - @result: references return value in postcondition
//   - @entry(expr): captures entry/old value of expression
//
// =============================================================================

#include "05_codegen/intrinsics/intrinsics_decls.h"

#include <array>

#include "00_core/assert_spec.h"

namespace cursive::codegen {

// =============================================================================
// Intrinsic Table
// =============================================================================

namespace {

// Static table of intrinsic metadata.
// Order must match IntrinsicKind enum.
constexpr std::array<IntrinsicInfo, static_cast<std::size_t>(IntrinsicKind::Count)> kIntrinsics = {{
    // Contract intrinsics
    {IntrinsicKind::Result, "@result", false, true, false, 0, 0},
    {IntrinsicKind::Entry, "@entry", false, true, false, 0, 1},

    // Type intrinsics
    {IntrinsicKind::SizeOf, "sizeof", false, false, true, 1, 0},
    {IntrinsicKind::AlignOf, "alignof", false, false, true, 1, 0},

    // Memory intrinsics
    {IntrinsicKind::Transmute, "transmute", true, false, false, 2, 1},

    // Modal intrinsics
    {IntrinsicKind::Widen, "widen", false, false, false, 0, 1},
}};

}  // namespace

// =============================================================================
// Intrinsic Lookup Implementation
// =============================================================================

const IntrinsicInfo* LookupIntrinsic(std::string_view name) {
  for (const auto& info : kIntrinsics) {
    if (info.name == name) {
      return &info;
    }
  }
  return nullptr;
}

bool IsIntrinsic(std::string_view name) {
  return LookupIntrinsic(name) != nullptr;
}

bool IsContractIntrinsic(IntrinsicKind kind) {
  return kind == IntrinsicKind::Result || kind == IntrinsicKind::Entry;
}

bool RequiresUnsafe(IntrinsicKind kind) {
  if (kind >= IntrinsicKind::Count) {
    return false;
  }
  return kIntrinsics[static_cast<std::size_t>(kind)].requires_unsafe;
}

bool IsComptime(IntrinsicKind kind) {
  if (kind >= IntrinsicKind::Count) {
    return false;
  }
  return kIntrinsics[static_cast<std::size_t>(kind)].is_comptime;
}

// =============================================================================
// Contract Intrinsic Support Implementation
// =============================================================================

bool ResultValidInContext(ContractContext ctx) {
  // @result is only valid in postcondition context
  return ctx == ContractContext::Postcondition;
}

bool EntryValidInContext(ContractContext ctx) {
  // @entry is only valid in postcondition context
  return ctx == ContractContext::Postcondition;
}

// =============================================================================
// Transmute Support Implementation
// =============================================================================

bool TransmuteValid(
    [[maybe_unused]] const analysis::TypeRef& from,
    [[maybe_unused]] const analysis::TypeRef& to,
    std::uint64_t from_size,
    std::uint64_t to_size) {
  // Transmute requires sizes to match exactly
  return from_size == to_size;
}

// =============================================================================
// Widen Support Implementation
// =============================================================================

bool WidenValid(const analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  // Widen is valid for modal state types
  return std::holds_alternative<analysis::TypeModalState>(type->node);
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorIntrinsicRules() {
  // Contract intrinsics
  SPEC_RULE("Contract-Result-Valid");
  SPEC_RULE("Contract-Entry-Valid");
  SPEC_RULE("Contract-Entry-Bitcopy");
  SPEC_RULE("Contract-Entry-Clone");

  // Type intrinsics
  SPEC_RULE("SizeOf-Valid");
  SPEC_RULE("AlignOf-Valid");

  // Transmute
  SPEC_RULE("Transmute-Valid");
  SPEC_RULE("Transmute-SizeMatch");
  SPEC_RULE("Transmute-Unsafe");

  // Widen
  SPEC_RULE("Widen-Modal");
  SPEC_RULE("Widen-State");
}

}  // namespace cursive::codegen
