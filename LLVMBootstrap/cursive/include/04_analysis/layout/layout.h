#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "00_core/int128.h"
#include "01_project/target_profile.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis::layout {

struct Layout {
  std::uint64_t size = 0;
  std::uint64_t align = 1;
};

struct LoweredAsyncType {
  std::vector<std::string> states;
  cursive::analysis::TypeRef resume_type;
};

struct RecordLayout {
  Layout layout;
  std::vector<std::uint64_t> offsets;
};

struct TupleField {
  std::size_t index = 0;
  cursive::analysis::TypeRef type;
};

struct RecordLayoutOptions {
  bool packed = false;
  std::optional<std::uint64_t> min_align;
};

struct EnumLayout {
  Layout layout;
  std::string disc_type;
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
};

struct EnumPayloadMemberLayout {
  cursive::analysis::TypeRef type;
  std::uint64_t offset = 0;
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
};

struct EnumLayoutOptions {
  std::optional<std::string> disc_type;
  std::optional<std::uint64_t> min_align;
};

struct UnionLayout {
  Layout layout;
  bool niche = false;
  std::optional<Layout> niche_payload_layout;
  std::optional<std::string> disc_type;
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
  std::vector<cursive::analysis::TypeRef> member_list;
};

struct ModalLayout {
  Layout layout;
  bool niche = false;
  std::optional<Layout> niche_payload_layout;
  std::optional<std::string> disc_type;
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
};

struct DynLayout {
  Layout layout;
  std::vector<cursive::analysis::TypeRef> fields;
};

struct LayoutEnv {
  cursive::project::TargetProfile target_profile =
      cursive::project::TargetProfile::X86_64SysV;
  std::uint64_t ptr_size = 8;
  std::uint64_t ptr_align = 8;
};

LayoutEnv LayoutEnvOf(cursive::project::TargetProfile target_profile);
LayoutEnv LayoutEnvOf(const cursive::analysis::ScopeContext& ctx);
std::uint64_t PtrSize(const LayoutEnv& env);
std::uint64_t PtrAlign(const LayoutEnv& env);
std::uint64_t PtrSize(const cursive::analysis::ScopeContext& ctx);
std::uint64_t PtrAlign(const cursive::analysis::ScopeContext& ctx);

// Compatibility constants for code that is not yet target-profile parameterized.
// Canonical layout queries should use LayoutEnvOf/PtrSize/PtrAlign.
constexpr std::uint64_t kPtrSize = 8;
constexpr std::uint64_t kPtrAlign = 8;

std::optional<std::uint64_t> PrimSize(const LayoutEnv& env,
                                      std::string_view name);
std::optional<std::uint64_t> PrimAlign(const LayoutEnv& env,
                                       std::string_view name);
std::optional<std::uint64_t> PrimSize(
    const cursive::analysis::ScopeContext& ctx,
    std::string_view name);
std::optional<std::uint64_t> PrimAlign(
    const cursive::analysis::ScopeContext& ctx,
    std::string_view name);
std::optional<std::uint64_t> PrimSize(std::string_view name);
std::optional<std::uint64_t> PrimAlign(std::string_view name);

std::optional<cursive::analysis::TypeRef> LowerTypeForLayout(
    const cursive::analysis::ScopeContext& ctx,
    const std::shared_ptr<cursive::ast::Type>& type);

std::optional<LoweredAsyncType> LowerAsyncType(
    const cursive::analysis::TypeRef& type);
std::optional<LoweredAsyncType> LowerAsyncType(
    const cursive::analysis::AsyncSig& sig);

std::optional<Layout> LayoutOf(const cursive::analysis::ScopeContext& ctx,
                               const cursive::analysis::TypeRef& type);
std::optional<std::uint64_t> SizeOf(const cursive::analysis::ScopeContext& ctx,
                                    const cursive::analysis::TypeRef& type);
std::optional<std::uint64_t> AlignOf(const cursive::analysis::ScopeContext& ctx,
                                     const cursive::analysis::TypeRef& type);

std::optional<RecordLayout> RecordLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const std::vector<cursive::analysis::TypeRef>& fields,
    const RecordLayoutOptions& options = {});

std::vector<TupleField> TupleFields(
    const std::vector<cursive::analysis::TypeRef>& elems);

std::optional<RecordLayout> TupleLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const std::vector<cursive::analysis::TypeRef>& elems);

std::optional<RecordLayout> RangeLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeRef& type);

std::optional<EnumLayout> EnumLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const EnumLayoutOptions& options = {});
std::optional<EnumLayout> EnumLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    const EnumLayoutOptions& options = {});

std::optional<EnumPayloadMemberLayout> EnumTuplePayloadMemberLayout(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const cursive::ast::VariantDecl& variant,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    std::size_t index);
std::optional<EnumPayloadMemberLayout> EnumRecordPayloadMemberLayout(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const cursive::ast::VariantDecl& variant,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    std::string_view field_name);

RecordLayoutOptions ResolveRecordLayoutOptions(
    const cursive::ast::AttributeList& attrs);
EnumLayoutOptions ResolveEnumLayoutOptions(
    const cursive::ast::AttributeList& attrs);

std::optional<UnionLayout> UnionLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeUnion& uni);

std::optional<ModalLayout> ModalLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::ModalDecl& decl,
    const std::vector<cursive::analysis::TypeRef>& generic_args = {});

DynLayout DynLayoutOf(const cursive::analysis::ScopeContext& ctx);
DynLayout DynLayoutOf();

// Value representations for ValueBits helpers.

struct Value;

struct BoolVal { bool value = false; };
struct CharVal { std::uint32_t value = 0; };
struct IntVal { std::string type; core::UInt128 value; };
struct FloatVal { std::string type; std::uint64_t bits = 0; };
struct UnitVal {};
struct PtrVal { cursive::analysis::PtrState state; std::uint64_t addr = 0; };
struct RawPtrVal { cursive::analysis::RawPtrQual qual; std::uint64_t addr = 0; };
struct TupleVal { std::vector<struct Value> elements; };
struct ArrayVal { std::vector<struct Value> elements; };
struct SliceVal { RawPtrVal ptr; std::uint64_t length = 0; };

// Value representation for range types (distinct from checks.h RangeVal)
enum class ValueRangeKind {
  To,
  ToInclusive,
  Full,
  From,
  Exclusive,
  Inclusive,
};

struct ValueRangeVal {
  ValueRangeKind kind = ValueRangeKind::Full;
  std::optional<std::uint64_t> lo;
  std::optional<std::uint64_t> hi;
};

struct RecordVal {
  std::vector<std::pair<std::string, struct Value>> fields;
};

struct EnumPayloadTupleVal { std::vector<struct Value> elements; };
struct EnumPayloadRecordVal { std::vector<std::pair<std::string, struct Value>> fields; };
using EnumPayloadVal = std::variant<EnumPayloadTupleVal, EnumPayloadRecordVal>;

struct EnumVal {
  std::string variant;
  std::optional<EnumPayloadVal> payload;
};

struct ModalVal {
  std::string state;
  std::shared_ptr<struct Value> payload;
};

struct UnionVal {
  cursive::analysis::TypeRef member;
  std::shared_ptr<struct Value> value;
};

struct DynamicVal {
  std::uint64_t data = 0;
  std::uint64_t vtable = 0;
};

struct StringVal { std::vector<std::uint8_t> bytes; };
struct BytesVal { std::vector<std::uint8_t> bytes; };

struct Value {
  std::variant<BoolVal,
               CharVal,
               IntVal,
               FloatVal,
               UnitVal,
               PtrVal,
               RawPtrVal,
               TupleVal,
               ArrayVal,
               SliceVal,
               ValueRangeVal,
               RecordVal,
               EnumVal,
               ModalVal,
               UnionVal,
               DynamicVal,
               StringVal,
               BytesVal>
      node;
};

std::optional<Value> TupleValue(const TupleVal& tuple, std::size_t index);
std::optional<TupleVal> TupleUpdate(const TupleVal& tuple,
                                    std::size_t index,
                                    Value value);
const Value* FieldValue(const RecordVal& record, std::string_view name);
std::optional<RecordVal> FieldUpdate(const RecordVal& record,
                                     std::string_view name,
                                     Value value);
std::optional<ArrayVal> IndexUpdate(const ArrayVal& array,
                                    std::size_t index,
                                    Value value);
std::size_t SliceLen(const ArrayVal& array);
std::optional<std::size_t> SliceLen(const Value& value);
std::optional<Value> SliceElem(const ArrayVal& array, std::size_t index);

std::optional<std::vector<std::uint8_t>> EncodeConst(
    const cursive::analysis::TypeRef& type,
    const cursive::ast::Token& lit);

// Decode a string literal lexeme into its UTF-8 byte sequence.
std::optional<std::vector<std::uint8_t>> DecodeStringLiteralBytes(
    std::string_view lexeme);

bool ValidValue(const cursive::analysis::ScopeContext& ctx,
                const cursive::analysis::TypeRef& type,
                const std::vector<std::uint8_t>& bits);

std::optional<std::vector<std::uint8_t>> ValueBits(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeRef& type,
    const Value& value);

}  // namespace cursive::analysis::layout
