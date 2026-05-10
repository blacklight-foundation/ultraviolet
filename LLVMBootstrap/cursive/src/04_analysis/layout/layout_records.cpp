// =============================================================================
// MIGRATION MAPPING: layout_records.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1.3 Record Layout Without [[layout(C)]] (lines 14577-14631)
//   - AlignUp formula (line 14579)
//   - Offsets computation (lines 14580-14581)
//   - RecordAlign computation (lines 14582-14583)
//   - RecordSize computation (lines 14584-14585)
//   - Layout-Record-Empty rule (lines 14588-14590)
//   - Layout-Record-Cons rule (lines 14592-14595)
//   - Size-Record, Align-Record, Layout-Record rules (lines 14597-14610)
//   - FieldOffset helper (line 14612)
//   - Type Alias layout rules (lines 14614-14630)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_records.cpp
//   - AlignUp helper (lines 10-19)
//   - RecordLayoutOf function (lines 23-52)
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (Layout, RecordLayout structs)
//   - cursive/include/04_analysis/scopes.h (ScopeContext)
//   - cursive/include/04_analysis/types/types.h (TypeRef)
//   - SizeOf, AlignOf functions from layout dispatch
//
// REFACTORING NOTES:
//   1. AlignUp(x, a) = ceil(x/a) * a for a > 0
//   2. Empty records: size=0, align=1, offsets=[]
//   3. Non-empty records: sequential field layout with alignment padding
//   4. Offset[0] = 0, Offset[i] = AlignUp(Offset[i-1] + Size[i-1], Align[i])
//   5. RecordAlign = max alignment of all fields
//   6. RecordSize = AlignUp(last_offset + last_size, record_align)
//   7. Type alias layout delegates to underlying type
//   8. Consider [[layout(C)]] attribute handling in separate file
//
// LAYOUT ALGORITHM:
//   1. For each field in declaration order:
//      a. Compute offset = AlignUp(current_offset, field_align)
//      b. Store offset in offsets array
//      c. Update current_offset = offset + field_size
//      d. Update max_align = max(max_align, field_align)
//   2. Final size = AlignUp(current_offset, max_align)
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>

#include "00_core/assert_spec.h"

namespace cursive::analysis::layout {
namespace {

std::uint64_t AlignUp(std::uint64_t value, std::uint64_t align) {
  if (align == 0) {
    return value;
  }
  const std::uint64_t rem = value % align;
  if (rem == 0) {
    return value;
  }
  return value + (align - rem);
}

}  // namespace

std::optional<RecordLayout> RecordLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const std::vector<cursive::analysis::TypeRef>& fields,
    const RecordLayoutOptions& options) {
  const bool packed = options.packed;
  if (fields.empty()) {
    SPEC_RULE("Layout-Record-Empty");
    std::uint64_t record_align = 1;
    if (!packed && options.min_align.has_value()) {
      record_align = std::max(record_align, *options.min_align);
    }
    return RecordLayout{Layout{0, record_align}, {}};
  }
  SPEC_RULE("Layout-Record-Cons");
  std::vector<std::uint64_t> offsets;
  offsets.reserve(fields.size());
  std::uint64_t offset = 0;
  std::uint64_t max_align = 1;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const auto field_layout = LayoutOf(ctx, fields[i]);
    if (!field_layout.has_value()) {
      return std::nullopt;
    }
    const std::uint64_t align = packed ? 1 : field_layout->align;
    const std::uint64_t size = field_layout->size;
    if (i == 0) {
      offset = 0;
    } else {
      offset = AlignUp(offset, align);
    }
    offsets.push_back(offset);
    max_align = std::max(max_align, align);
    offset += size;
  }
  if (packed) {
    max_align = 1;
  } else if (options.min_align.has_value()) {
    max_align = std::max(max_align, *options.min_align);
  }
  const std::uint64_t size = AlignUp(offset, max_align);
  return RecordLayout{Layout{size, max_align}, offsets};
}

}  // namespace cursive::analysis::layout
