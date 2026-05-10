// =============================================================================
// MIGRATION MAPPING: layout_dynobj.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1.8 Dynamic Class Object Layout (lines 15135-15152)
//   - DynFields definition (line 15137)
//   - Layout-DynamicClass rule (lines 15140-15142)
//   - Size-DynamicClass rule (lines 15144-15147)
//   - Align-DynamicClass rule (lines 15149-15152)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_dynobj.cpp
//   - DynLayoutOf function (if exists)
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (Layout struct)
//   - cursive/include/04_analysis/types/types.h (TypeDynamic)
//
// REFACTORING NOTES:
//   1. Dynamic class objects are fat pointers: (data_ptr, vtable_ptr)
//   2. DynFields(Cl) = [{data, *imm ()}, {vtable, *imm VTable}]
//   3. Size = 2 * PtrSize (16 bytes on 64-bit)
//   4. Align = PtrAlign (8 bytes on 64-bit)
//   5. Data pointer points to the concrete type instance
//   6. VTable pointer points to the class implementation vtable
//   7. All dynamic class objects have the same layout regardless of class
//
// FAT POINTER STRUCTURE:
//   struct DynObj {
//     *imm () data;      // Erased pointer to concrete data
//     *imm VTable vtable; // Pointer to vtable for this class
//   }
//
// VTABLE STRUCTURE (see vtable_emit.cpp):
//   struct VTable {
//     usize size;     // Size of concrete type
//     usize align;    // Alignment of concrete type
//     *imm () drop;   // Drop function pointer
//     *imm ()[] slots; // Method function pointers
//   }
// =============================================================================

#include "04_analysis/layout/layout.h"

#include "00_core/assert_spec.h"

namespace cursive::analysis::layout {

DynLayout DynLayoutOf(const cursive::analysis::ScopeContext& ctx) {
  SPEC_RULE("Layout-DynamicClass");
  DynLayout out;
  const std::uint64_t ptr_size = PtrSize(ctx);
  const std::uint64_t ptr_align = PtrAlign(ctx);
  out.layout = Layout{2 * ptr_size, ptr_align};
  out.fields.push_back(
      cursive::analysis::MakeTypeRawPtr(cursive::analysis::RawPtrQual::Imm,
                                        cursive::analysis::MakeTypePrim("()")));
  out.fields.push_back(
      cursive::analysis::MakeTypeRawPtr(cursive::analysis::RawPtrQual::Imm,
                                        cursive::analysis::MakeTypePath({"VTable"})));
  return out;
}

DynLayout DynLayoutOf() {
  cursive::analysis::ScopeContext ctx;
  ctx.target_profile = cursive::project::TargetProfile::X86_64SysV;
  return DynLayoutOf(ctx);
}

}  // namespace cursive::analysis::layout
