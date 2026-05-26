// =============================================================================
// MIGRATION MAPPING: layout_tuples.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.1.6 Aggregate Layouts - Tuples (lines 14953-14982)
//   - TupleFields definition (line 14957)
//   - Layout-Tuple-Empty rule (lines 14960-14962)
//   - Layout-Tuple-Cons rule (lines 14964-14967)
//   - Size-Tuple, Align-Tuple, Layout-Tuple rules (lines 14969-14982)
//
// SOURCE FILE: No direct source file - computed via RecordLayoutOf
//   - TupleLayout computed by treating tuple elements as positional fields
//   - Uses same algorithm as RecordLayoutOf with synthetic field names
//
// DEPENDENCIES:
//   - ultraviolet/include/04_analysis/layout/layout.h (RecordLayout struct)
//   - RecordLayoutOf for layout computation
//   - analysis::TypeRef for element types
//
// REFACTORING NOTES:
//   1. TupleFields([T1, ..., Tn]) = [{0, T1}, ..., {n-1, Tn}]
//   2. Empty tuple () has size=0, align=1
//   3. Single-element tuple (T;) has same layout as T with alignment
//   4. Multi-element tuples use record layout algorithm
//   5. Tuple elements accessed by positional index (0, 1, 2, ...)
//   6. Offsets array provides element positions
//
// TUPLE LAYOUT ALGORITHM:
//   - Convert tuple types to synthetic record fields
//   - Apply RecordLayoutOf
//   - Return RecordLayout with computed offsets
//
// EXAMPLE:
//   (i32, bool, i64):
//   - i32 at offset 0 (4 bytes, align 4)
//   - bool at offset 4 (1 byte, align 1)
//   - 3 bytes padding
//   - i64 at offset 8 (8 bytes, align 8)
//   - Total: 16 bytes, align 8
// =============================================================================

#include "04_analysis/layout/layout.h"

#include "00_core/assert_spec.h"

// Tuple layout is computed directly in layout_dispatch.cpp via RecordLayoutOf.
// This file exists for organizational clarity but the implementation delegates
// to the record layout algorithm since tuples are laid out identically to
// records with positional fields.
//
// The LayoutOf function in layout_dispatch.cpp handles TypeTuple by calling:
//   RecordLayoutOf(ctx, tuple->elements)
//
// This achieves the spec's TupleFields transformation implicitly.

namespace ultraviolet::analysis::layout {

std::vector<TupleField> TupleFields(
    const std::vector<ultraviolet::analysis::TypeRef>& elems) {
  std::vector<TupleField> out;
  out.reserve(elems.size());
  for (std::size_t i = 0; i < elems.size(); ++i) {
    TupleField field;
    field.index = i;
    field.type = elems[i];
    out.push_back(field);
  }
  return out;
}

std::optional<RecordLayout> TupleLayoutOf(
    const ultraviolet::analysis::ScopeContext& ctx,
    const std::vector<ultraviolet::analysis::TypeRef>& elems) {
  const auto fields = TupleFields(elems);
  std::vector<ultraviolet::analysis::TypeRef> field_types;
  field_types.reserve(fields.size());
  for (const auto& field : fields) {
    field_types.push_back(field.type);
  }
  return RecordLayoutOf(ctx, field_types);
}

}  // namespace ultraviolet::analysis::layout
