// =============================================================================
// MIGRATION MAPPING: layout_primitives.cpp
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
//   - Section 6.1.1 Primitive Layout and Encoding (lines 14351-14476)
//   - PtrSize = 8, PtrAlign = 8 (lines 14353-14355)
//   - PrimSize definitions (lines 14357-14375)
//   - PrimAlign definitions (lines 14377-14395)
//   - Layout-Prim rule (lines 14409-14412)
//   - Size-Prim rule (lines 14399-14402)
//   - Align-Prim rule (lines 14404-14407)
//   - Encoding rules: Encode-Bool through Encode-RawPtr-Null (lines 14414-14456)
//   - Validity rules: Valid-Bool through Valid-Never (lines 14457-14475)
//
// SOURCE FILE: ultraviolet-bootstrap/src/04_codegen/layout/layout_primitives.cpp
//   - PrimSize function (lines 7-22)
//   - PrimAlign function (lines 24-39)
//
// DEPENDENCIES:
//   - ultraviolet/include/04_analysis/layout/layout.h (Layout struct, constants)
//   - ultraviolet/include/00_core/assert_spec.h (SPEC_RULE macro)
//
// REFACTORING NOTES:
//   1. Pointer size and alignment come from the selected target profile.
//   2. kPtrSize/kPtrAlign remain compatibility defaults for no-context callers.
//   3. All integer types: i8, i16, i32, i64, i128, u8, u16, u32, u64, u128
//   4. Float types: f16, f32, f64
//   5. Special types: bool (1 byte), char (4 bytes for UTF-32)
//   6. Size types: usize, isize (pointer-sized)
//   7. Zero-sized types: () and ! (unit and never)
//   8. Consider adding EncodeConst for constant evaluation
//   9. ValidValue predicates needed for union niche optimization
//
// PRIMITIVE SIZE TABLE:
//   i8/u8 = 1, i16/u16 = 2, i32/u32 = 4, i64/u64 = 8, i128/u128 = 16
//   f16 = 2, f32 = 4, f64 = 8
//   bool = 1, char = 4
//   usize/isize = PtrSize (8)
//   ()/! = 0
//
// PRIMITIVE ALIGN TABLE:
//   Same as sizes except: () = 1, ! = 1
// =============================================================================

#include "04_analysis/layout/layout.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::analysis::layout {

LayoutEnv LayoutEnvOf(ultraviolet::project::TargetProfile target_profile) {
  LayoutEnv env;
  env.target_profile = target_profile;
  env.ptr_size = static_cast<std::uint64_t>(
      ultraviolet::project::PtrSizeBytes(target_profile));
  env.ptr_align = env.ptr_size;
  return env;
}

LayoutEnv LayoutEnvOf(const ultraviolet::analysis::ScopeContext& ctx) {
  return LayoutEnvOf(
      ctx.target_profile.value_or(ultraviolet::project::TargetProfile::X86_64SysV));
}

std::uint64_t PtrSize(const LayoutEnv& env) {
  return env.ptr_size;
}

std::uint64_t PtrAlign(const LayoutEnv& env) {
  return env.ptr_align;
}

std::uint64_t PtrSize(const ultraviolet::analysis::ScopeContext& ctx) {
  return PtrSize(LayoutEnvOf(ctx));
}

std::uint64_t PtrAlign(const ultraviolet::analysis::ScopeContext& ctx) {
  return PtrAlign(LayoutEnvOf(ctx));
}

std::optional<std::uint64_t> PrimSize(const LayoutEnv& env,
                                      std::string_view name) {
  SPEC_RULE("Size-Prim");
  if (name == "i8" || name == "u8") return 1;
  if (name == "i16" || name == "u16") return 2;
  if (name == "i32" || name == "u32") return 4;
  if (name == "i64" || name == "u64") return 8;
  if (name == "i128" || name == "u128") return 16;
  if (name == "f16") return 2;
  if (name == "f32") return 4;
  if (name == "f64") return 8;
  if (name == "bool") return 1;
  if (name == "char") return 4;
  if (name == "usize" || name == "isize") return PtrSize(env);
  if (name == "()" || name == "!") return 0;
  return std::nullopt;
}

std::optional<std::uint64_t> PrimAlign(const LayoutEnv& env,
                                       std::string_view name) {
  SPEC_RULE("Align-Prim");
  if (name == "i8" || name == "u8") return 1;
  if (name == "i16" || name == "u16") return 2;
  if (name == "i32" || name == "u32") return 4;
  if (name == "i64" || name == "u64") return 8;
  if (name == "i128" || name == "u128") return 16;
  if (name == "f16") return 2;
  if (name == "f32") return 4;
  if (name == "f64") return 8;
  if (name == "bool") return 1;
  if (name == "char") return 4;
  if (name == "usize" || name == "isize") return PtrAlign(env);
  if (name == "()" || name == "!") return 1;
  return std::nullopt;
}

std::optional<std::uint64_t> PrimSize(
    const ultraviolet::analysis::ScopeContext& ctx,
    std::string_view name) {
  return PrimSize(LayoutEnvOf(ctx), name);
}

std::optional<std::uint64_t> PrimAlign(
    const ultraviolet::analysis::ScopeContext& ctx,
    std::string_view name) {
  return PrimAlign(LayoutEnvOf(ctx), name);
}

std::optional<std::uint64_t> PrimSize(std::string_view name) {
  return PrimSize(LayoutEnvOf(ultraviolet::project::TargetProfile::X86_64SysV),
                  name);
}

std::optional<std::uint64_t> PrimAlign(std::string_view name) {
  return PrimAlign(LayoutEnvOf(ultraviolet::project::TargetProfile::X86_64SysV),
                   name);
}

}  // namespace ultraviolet::analysis::layout
