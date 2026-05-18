/*
 * =============================================================================
 * MIGRATION MAPPING: string_bytes.cpp
 * =============================================================================
 *
 * SPEC REFERENCE:
 *   - SPECIFICATION.md, Section 5.6 "String and Bytes Types" (lines 13160-13300)
 *   - SPECIFICATION.md, Section 5.6.1 "String States" (lines 13170-13220)
 *   - SPECIFICATION.md, Section 5.6.2 "Bytes States" (lines 13230-13280)
 *   - SPECIFICATION.md, Section 5.6.3 "String/Bytes Methods" (lines 13290-13400)
 *
 * SOURCE FILE:
 *   - ultraviolet-bootstrap/src/03_analysis/memory/string_bytes.cpp (lines 1-285)
 *
 * FUNCTIONS TO MIGRATE:
 *   - BuildStringType() -> TypeRef                                 [lines 15-50]
 *       Construct the string modal type
 *   - BuildBytesType() -> TypeRef                                  [lines 55-90]
 *       Construct the bytes modal type
 *   - StringMethodSignatures() -> Vec<ProcSig>                     [lines 95-180]
 *       Get method signatures for string type
 *   - BytesMethodSignatures() -> Vec<ProcSig>                      [lines 185-250]
 *       Get method signatures for bytes type
 *   - IsStringType(TypeRef ty) -> bool                             [lines 255-265]
 *       Check if type is string@View or string@Managed
 *   - IsBytesType(TypeRef ty) -> bool                              [lines 270-280]
 *       Check if type is bytes@View or bytes@Managed
 *   - StringStateOf(TypeRef ty) -> StringState                     [lines 282-285]
 *       Get @View or @Managed state of string type
 *
 * DEPENDENCIES:
 *   - Modal type infrastructure
 *   - HeapAllocator for @Managed allocation
 *   - Method signature building
 *
 * REFACTORING NOTES:
 *   1. string is modal: @View (non-owning view) vs @Managed (heap allocated)
 *   2. bytes is modal: @View (non-owning view) vs @Managed (heap allocated)
 *   3. String literals are string@View
 *   4. @Managed requires HeapAllocator capability
 *   5. Conversion @View -> @Managed requires allocation
 *   6. @View cannot outlive its source
 *   7. Consider UTF-8 validation for string type
 *
 * TYPE SIGNATURES:
 *   string@View:
 *     - length(~) -> usize
 *     - is_empty(~) -> bool
 *     - as_bytes(~) -> bytes@View
 *   string@Managed:
 *     - (all @View methods)
 *     - push(~!, ch: char) -> ()
 *     - from(view: string@View, heap: $HeapAllocator)
 *       -> Outcome<unique string@Managed, AllocationError>
 *   bytes@View:
 *     - length(~) -> usize
 *     - get(~, idx: usize) -> u8 | ()
 *   bytes@Managed:
 *     - (all @View methods)
 *     - push(~!, byte: u8) -> ()
 *     - with_capacity(cap: usize, heap: $HeapAllocator)
 *       -> Outcome<unique bytes@Managed, AllocationError>
 *
 * DIAGNOSTIC CODES:
 *   - E-STR-0001: @View outlives source
 *   - E-STR-0002: @Managed without allocator
 *   - E-STR-0003: Invalid UTF-8 in string
 *
 * =============================================================================
 */

#include "04_analysis/memory/string_bytes.h"

#include <array>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/outcome.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsStringBytes() {
  SPEC_DEF("StringBytesBuiltinTable", "5.8");
  SPEC_DEF("StringBytesBuiltinSig", "5.8");
  SPEC_DEF("StringBytesJudg", "5.8");
}

static inline void TouchStringBytesRules() {
  SPEC_RULE("StringFrom-Ok");
  SPEC_RULE("StringFrom-Err");
  SPEC_RULE("StringAsView-Ok");
  SPEC_RULE("StringToManaged-Ok");
  SPEC_RULE("StringToManaged-Err");
  SPEC_RULE("StringCloneWith-Ok");
  SPEC_RULE("StringCloneWith-Err");
  SPEC_RULE("StringAppend-Ok");
  SPEC_RULE("StringAppend-Err");
  SPEC_RULE("StringLength");
  SPEC_RULE("StringIsEmpty");
  SPEC_RULE("BytesWithCapacity-Ok");
  SPEC_RULE("BytesWithCapacity-Err");
  SPEC_RULE("BytesFromSlice-Ok");
  SPEC_RULE("BytesFromSlice-Err");
  SPEC_RULE("BytesAsView-Ok");
  SPEC_RULE("BytesToManaged-Ok");
  SPEC_RULE("BytesToManaged-Err");
  SPEC_RULE("BytesView-Ok");
  SPEC_RULE("BytesViewString-Ok");
  SPEC_RULE("BytesAsSlice-Ok");
  SPEC_RULE("BytesAppend-Ok");
  SPEC_RULE("BytesAppend-Err");
  SPEC_RULE("BytesLength");
  SPEC_RULE("BytesIsEmpty");
}

static bool IsStringPath(const ast::ModulePath& path) {
  return path.size() == 1 && IdEq(path[0], "string");
}

static bool IsBytesPath(const ast::ModulePath& path) {
  return path.size() == 1 && IdEq(path[0], "bytes");
}

static TypeRef AllocErrorType() {
  return MakeTypePath({"AllocationError"});
}

static TypeRef HeapAllocatorType() {
  return MakeTypeDynamic({"HeapAllocator"});
}

static TypeRef StringViewType() {
  return MakeTypeString(StringState::View);
}

static TypeRef StringManagedType() {
  return MakeTypeString(StringState::Managed);
}

static TypeRef BytesViewType() {
  return MakeTypeBytes(BytesState::View);
}

static TypeRef BytesManagedType() {
  return MakeTypeBytes(BytesState::Managed);
}

static TypeRef ConstType(const TypeRef& base) {
  return MakeTypePerm(Permission::Const, base);
}

static TypeRef UniqueType(const TypeRef& base) {
  return MakeTypePerm(Permission::Unique, base);
}

static TypeRef UniqueStringManagedType() {
  return UniqueType(StringManagedType());
}

static TypeRef UniqueBytesManagedType() {
  return UniqueType(BytesManagedType());
}

static TypeRef SliceU8Type() {
  return MakeTypeSlice(MakeTypePrim("u8"));
}

static TypeRef Outcome(TypeRef value, TypeRef error) {
  return MakeOutcomeType(std::move(value), std::move(error));
}

static TypeRef MakeFunc(std::vector<TypeFuncParam> params, TypeRef ret) {
  return MakeTypeFunc(std::move(params), std::move(ret));
}

static StringBytesBuiltinMethodSig MakeMethodSig(
    Permission recv_perm,
    TypeRef recv_type,
    std::vector<TypeFuncParam> params,
    TypeRef ret) {
  return {recv_perm, std::move(recv_type), std::move(params), std::move(ret)};
}

static std::optional<TypeRef> StringBuiltinType(std::string_view name) {
  if (IdEq(name, "from")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, StringViewType()},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueStringManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "as_view")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringManagedType())},
    };
    return MakeFunc(std::move(params), StringViewType());
  }
  if (IdEq(name, "slice")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringViewType())},
        {std::nullopt, MakeTypePrim("usize")},
        {std::nullopt, MakeTypePrim("usize")},
    };
    return MakeFunc(std::move(params), StringViewType());
  }
  if (IdEq(name, "to_managed")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringViewType())},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueStringManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "clone_with")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringManagedType())},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueStringManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "append")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, UniqueType(StringManagedType())},
        {std::nullopt, StringViewType()},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(MakeTypePrim("()"), AllocErrorType()));
  }
  if (IdEq(name, "length")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringViewType())},
    };
    return MakeFunc(std::move(params), MakeTypePrim("usize"));
  }
  if (IdEq(name, "is_empty")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(StringViewType())},
    };
    return MakeFunc(std::move(params), MakeTypePrim("bool"));
  }
  return std::nullopt;
}

static std::optional<TypeRef> BytesBuiltinType(std::string_view name) {
  if (IdEq(name, "with_capacity")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, MakeTypePrim("usize")},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueBytesManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "from_slice")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(SliceU8Type())},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueBytesManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "as_view")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(BytesManagedType())},
    };
    return MakeFunc(std::move(params), BytesViewType());
  }
  if (IdEq(name, "to_managed")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(BytesViewType())},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(UniqueBytesManagedType(), AllocErrorType()));
  }
  if (IdEq(name, "view")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(SliceU8Type())},
    };
    return MakeFunc(std::move(params), BytesViewType());
  }
  if (IdEq(name, "view_string")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, StringViewType()},
    };
    return MakeFunc(std::move(params), BytesViewType());
  }
  if (IdEq(name, "as_slice")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(BytesViewType())},
    };
    return MakeFunc(std::move(params), ConstType(SliceU8Type()));
  }
  if (IdEq(name, "append")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, UniqueType(BytesManagedType())},
        {std::nullopt, BytesViewType()},
        {std::nullopt, HeapAllocatorType()},
    };
    return MakeFunc(std::move(params),
                    Outcome(MakeTypePrim("()"), AllocErrorType()));
  }
  if (IdEq(name, "length")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(BytesViewType())},
    };
    return MakeFunc(std::move(params), MakeTypePrim("usize"));
  }
  if (IdEq(name, "is_empty")) {
    std::vector<TypeFuncParam> params = {
        {std::nullopt, ConstType(BytesViewType())},
    };
    return MakeFunc(std::move(params), MakeTypePrim("bool"));
  }
  return std::nullopt;
}

}  // namespace

bool IsStringBytesBuiltinPath(const ast::ModulePath& path) {
  SpecDefsStringBytes();
  return IsStringPath(path) || IsBytesPath(path);
}

bool IsStringBuiltinName(std::string_view name) {
  SpecDefsStringBytes();
  static constexpr std::array<std::string_view, 8> names = {
      "from", "as_view", "to_managed", "clone_with",
      "append", "slice", "length", "is_empty",
  };
  for (const auto& entry : names) {
    if (IdEq(name, entry)) {
      return true;
    }
  }
  return false;
}

bool IsBytesBuiltinName(std::string_view name) {
  SpecDefsStringBytes();
  static constexpr std::array<std::string_view, 10> names = {
      "with_capacity", "from_slice", "as_view", "to_managed",
      "view", "view_string", "as_slice", "append", "length", "is_empty",
  };
  for (const auto& entry : names) {
    if (IdEq(name, entry)) {
      return true;
    }
  }
  return false;
}

std::optional<TypeRef> LookupStringBytesBuiltinType(
    const ast::ModulePath& path,
    std::string_view name) {
  SpecDefsStringBytes();
  if (!IsStringBytesBuiltinPath(path)) {
    return std::nullopt;
  }
  TouchStringBytesRules();
  if (IsStringPath(path)) {
    return StringBuiltinType(name);
  }
  if (IsBytesPath(path)) {
    return BytesBuiltinType(name);
  }
  return std::nullopt;
}

std::optional<StringBytesBuiltinMethodSig> LookupStringBytesBuiltinMethodSig(
    const TypeRef& recv_base,
    std::string_view name) {
  SpecDefsStringBytes();
  TouchStringBytesRules();
  if (!recv_base) {
    return std::nullopt;
  }

  if (const auto* str = std::get_if<TypeString>(&recv_base->node)) {
    const auto any_string = MakeTypeString(std::nullopt);
    const auto managed_string = MakeTypeString(StringState::Managed);
    const auto view_string = MakeTypeString(StringState::View);
    if (IdEq(name, "length")) {
      return MakeMethodSig(Permission::Const, any_string, {},
                           MakeTypePrim("usize"));
    }
    if (IdEq(name, "is_empty")) {
      return MakeMethodSig(Permission::Const, any_string, {},
                           MakeTypePrim("bool"));
    }
    if (IdEq(name, "as_view") && str->state == StringState::Managed) {
      return MakeMethodSig(Permission::Const, managed_string, {}, view_string);
    }
    if (IdEq(name, "slice") && str->state == StringState::View) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, MakeTypePrim("usize")},
          {std::nullopt, MakeTypePrim("usize")},
      };
      return MakeMethodSig(Permission::Const, view_string, std::move(params),
                           view_string);
    }
    if (IdEq(name, "to_managed") && str->state == StringState::View) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, HeapAllocatorType()},
      };
      return MakeMethodSig(Permission::Const, view_string, std::move(params),
                           Outcome(UniqueStringManagedType(), AllocErrorType()));
    }
    if (IdEq(name, "clone_with") && str->state == StringState::Managed) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, HeapAllocatorType()},
      };
      return MakeMethodSig(Permission::Const, managed_string,
                           std::move(params),
                           Outcome(UniqueStringManagedType(), AllocErrorType()));
    }
    if (IdEq(name, "append") && str->state == StringState::Managed) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, StringViewType()},
          {std::nullopt, HeapAllocatorType()},
      };
      return MakeMethodSig(Permission::Unique, managed_string,
                           std::move(params),
                           Outcome(MakeTypePrim("()"), AllocErrorType()));
    }
    return std::nullopt;
  }

  if (const auto* bytes = std::get_if<TypeBytes>(&recv_base->node)) {
    const auto any_bytes = MakeTypeBytes(std::nullopt);
    const auto managed_bytes = MakeTypeBytes(BytesState::Managed);
    const auto view_bytes = MakeTypeBytes(BytesState::View);
    if (IdEq(name, "length")) {
      return MakeMethodSig(Permission::Const, any_bytes, {},
                           MakeTypePrim("usize"));
    }
    if (IdEq(name, "is_empty")) {
      return MakeMethodSig(Permission::Const, any_bytes, {},
                           MakeTypePrim("bool"));
    }
    if (IdEq(name, "as_slice")) {
      return MakeMethodSig(Permission::Const, any_bytes, {},
                           ConstType(SliceU8Type()));
    }
    if (IdEq(name, "as_view") && bytes->state == BytesState::Managed) {
      return MakeMethodSig(Permission::Const, managed_bytes, {}, view_bytes);
    }
    if (IdEq(name, "to_managed") && bytes->state == BytesState::View) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, HeapAllocatorType()},
      };
      return MakeMethodSig(Permission::Const, view_bytes, std::move(params),
                           Outcome(UniqueBytesManagedType(), AllocErrorType()));
    }
    if (IdEq(name, "append") && bytes->state == BytesState::Managed) {
      std::vector<TypeFuncParam> params = {
          {std::nullopt, BytesViewType()},
          {std::nullopt, HeapAllocatorType()},
      };
      return MakeMethodSig(Permission::Unique, managed_bytes,
                           std::move(params),
                           Outcome(MakeTypePrim("()"), AllocErrorType()));
    }
    return std::nullopt;
  }

  return std::nullopt;
}

}  // namespace ultraviolet::analysis
