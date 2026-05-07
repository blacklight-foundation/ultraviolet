// =============================================================================
// MIGRATION MAPPING: layout_aggregates.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1.6 Aggregate Layouts (lines 14953-15084)
//   - Tuples (lines 14955-14982)
//   - Arrays (lines 14984-14999)
//   - Slices (lines 15001-15013)
//   - Ranges (lines 15015-15048)
//   - Enums (lines 15050-15084)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_aggregates.cpp
//   - AlignUp helper (lines 10-19)
//   - DiscTypeName helper (lines 21-32)
//   - DiscTypeLayout helper (lines 34-45)
//   - RangeLayoutInternal (lines 47-55)
//   - RangeLayoutOf (lines 59-62)
//   - EnumLayoutOf (lines 65-133)
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (Layout, RecordLayout, EnumLayout)
//   - RecordLayoutOf, SizeOf, AlignOf functions
//   - LowerTypeForLayout for type resolution
//   - EnumDiscriminants for discriminant computation
//
// REFACTORING NOTES:
//   1. This file serves as central dispatch for aggregate layouts
//   2. Arrays: size = element_size * length, align = element_align
//      - Size-Array (lines 14986-14989)
//      - Align-Array (lines 14991-14994)
//   3. Slices: fat pointer (data_ptr, length)
//      - Size = 2 * PtrSize (16 bytes on 64-bit)
//      - Align = PtrAlign (8 bytes on 64-bit)
//   4. Helper functions used by specific layout files
//   5. DiscTypeName selects smallest integer type for discriminant
//   6. AlignUp is fundamental padding computation
//
// AGGREGATE TYPE SUMMARY:
//   - Tuple: sequential fields with padding
//   - Array: homogeneous elements, no padding between
//   - Slice: (ptr, len) fat pointer
//   - Range: (kind, lo, hi) struct
//   - Enum: discriminant + max variant payload
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>
#include <limits>

#include "00_core/assert_spec.h"
#include "04_analysis/composite/enums.h"
#include "04_analysis/generics/monomorphize.h"

namespace cursive::analysis::layout {
namespace {

std::optional<cursive::analysis::TypeSubst> BuildEnumLayoutSubstitution(
    const std::optional<cursive::ast::GenericParams>& generic_params,
    const std::vector<cursive::analysis::TypeRef>& generic_args) {
  if (!generic_params.has_value() || generic_params->params.empty()) {
    return cursive::analysis::TypeSubst{};
  }
  if (generic_args.size() > generic_params->params.size()) {
    return std::nullopt;
  }
  return cursive::analysis::BuildSubstitution(generic_params->params,
                                              generic_args);
}

std::optional<cursive::analysis::TypeRef> LowerEnumPayloadType(
    const cursive::analysis::ScopeContext& ctx,
    const std::shared_ptr<cursive::ast::Type>& type,
    const cursive::analysis::TypeSubst& subst) {
  const auto lowered = LowerTypeForLayout(ctx, type);
  if (!lowered.has_value()) {
    return std::nullopt;
  }
  if (subst.empty()) {
    return *lowered;
  }
  return cursive::analysis::InstantiateType(*lowered, subst);
}

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

std::optional<std::string> DiscTypeName(std::uint64_t max_disc) {
  if (max_disc <= 0xFFu) {
    return std::string("u8");
  }
  if (max_disc <= 0xFFFFu) {
    return std::string("u16");
  }
  if (max_disc <= 0xFFFFFFFFu) {
    return std::string("u32");
  }
  return std::string("u64");
}

std::optional<RecordLayout> RangeLayoutInternal(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  cursive::analysis::TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm =
            std::get_if<cursive::analysis::TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    if (const auto* refine =
            std::get_if<cursive::analysis::TypeRefine>(&stripped->node)) {
      stripped = refine->base;
      continue;
    }
    break;
  }
  if (!stripped) {
    return std::nullopt;
  }

  std::vector<cursive::analysis::TypeRef> fields;
  if (const auto* range =
          std::get_if<cursive::analysis::TypeRange>(&stripped->node)) {
    SPEC_RULE("Layout-Range");
    fields.push_back(range->base);
    fields.push_back(range->base);
  } else if (const auto* range = std::get_if<cursive::analysis::TypeRangeInclusive>(
                 &stripped->node)) {
    SPEC_RULE("Layout-RangeInclusive");
    fields.push_back(range->base);
    fields.push_back(range->base);
  } else if (const auto* range =
                 std::get_if<cursive::analysis::TypeRangeFrom>(&stripped->node)) {
    SPEC_RULE("Layout-RangeFrom");
    fields.push_back(range->base);
  } else if (const auto* range =
                 std::get_if<cursive::analysis::TypeRangeTo>(&stripped->node)) {
    SPEC_RULE("Layout-RangeTo");
    fields.push_back(range->base);
  } else if (const auto* range = std::get_if<
                 cursive::analysis::TypeRangeToInclusive>(&stripped->node)) {
    SPEC_RULE("Layout-RangeToInclusive");
    fields.push_back(range->base);
  } else if (std::holds_alternative<cursive::analysis::TypeRangeFull>(
                 stripped->node)) {
    SPEC_RULE("Layout-RangeFull");
  } else {
    return std::nullopt;
  }
  return RecordLayoutOf(ctx, fields);
}

}  // namespace

std::optional<RecordLayout> RangeLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeRef& type) {
  return RangeLayoutInternal(ctx, type);
}


std::optional<EnumLayout> EnumLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const EnumLayoutOptions& options) {
  const std::vector<cursive::analysis::TypeRef> generic_args;
  return EnumLayoutOf(ctx, decl, generic_args, options);
}

std::optional<EnumLayout> EnumLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    const EnumLayoutOptions& options) {
  if (decl.variants.empty()) {
    return std::nullopt;
  }
  const auto subst = BuildEnumLayoutSubstitution(decl.generic_params, generic_args);
  if (!subst.has_value()) {
    return std::nullopt;
  }
  const auto discs = cursive::analysis::EnumDiscriminants(decl);
  if (!discs.ok || discs.discs.empty()) {
    return std::nullopt;
  }

  std::string disc_type_name;
  if (options.disc_type.has_value()) {
    disc_type_name = *options.disc_type;
  } else {
    const auto inferred_disc_name = DiscTypeName(discs.max_disc);
    if (!inferred_disc_name.has_value()) {
      return std::nullopt;
    }
    disc_type_name = *inferred_disc_name;
  }
  const auto disc_size = PrimSize(ctx, disc_type_name);
  const auto disc_align = PrimAlign(ctx, disc_type_name);
  if (!disc_size.has_value() || !disc_align.has_value()) {
    return std::nullopt;
  }
  auto disc_layout = std::optional<Layout>(Layout{*disc_size, *disc_align});

  auto disc_max_value = [&](std::string_view name) -> std::optional<std::uint64_t> {
    if (name == "u8") {
      return 0xFFu;
    }
    if (name == "u16") {
      return 0xFFFFu;
    }
    if (name == "u32") {
      return 0xFFFFFFFFu;
    }
    if (name == "u64") {
      return std::numeric_limits<std::uint64_t>::max();
    }
    if (name == "i8") {
      return 0x7Fu;
    }
    if (name == "i16") {
      return 0x7FFFu;
    }
    if (name == "i32") {
      return 0x7FFFFFFFu;
    }
    if (name == "i64") {
      return 0x7FFFFFFFFFFFFFFFull;
    }
    return std::nullopt;
  };
  if (const auto max_value = disc_max_value(disc_type_name); !max_value.has_value()) {
    return std::nullopt;
  } else if (discs.max_disc > *max_value) {
    return std::nullopt;
  }

  if (!disc_layout.has_value()) {
    return std::nullopt;
  }

  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;

  for (const auto& variant : decl.variants) {
    std::optional<RecordLayout> layout;
    if (!variant.payload_opt.has_value()) {
      layout = RecordLayout{Layout{0, 1}, {}};
    } else if (const auto* tuple =
                   std::get_if<cursive::ast::VariantPayloadTuple>(
                       &*variant.payload_opt)) {
      std::vector<cursive::analysis::TypeRef> elems;
      elems.reserve(tuple->elements.size());
      for (const auto& elem : tuple->elements) {
        const auto lowered = LowerEnumPayloadType(ctx, elem, *subst);
        if (!lowered.has_value()) {
          return std::nullopt;
        }
        elems.push_back(*lowered);
      }
      layout = RecordLayoutOf(ctx, elems);
    } else if (const auto* rec =
                   std::get_if<cursive::ast::VariantPayloadRecord>(
                       &*variant.payload_opt)) {
      std::vector<cursive::analysis::TypeRef> fields;
      fields.reserve(rec->fields.size());
      for (const auto& field : rec->fields) {
        const auto lowered = LowerEnumPayloadType(ctx, field.type, *subst);
        if (!lowered.has_value()) {
          return std::nullopt;
        }
        fields.push_back(*lowered);
      }
      layout = RecordLayoutOf(ctx, fields);
    }

    if (!layout.has_value()) {
      return std::nullopt;
    }
    payload_size = std::max(payload_size, layout->layout.size);
    payload_align = std::max(payload_align, layout->layout.align);
  }

  SPEC_RULE("Layout-Enum-Tagged");
  std::uint64_t align = std::max(disc_layout->align, payload_align);
  if (options.min_align.has_value()) {
    align = std::max(align, *options.min_align);
  }
  const std::uint64_t size = AlignUp(disc_layout->size + payload_size, align);

  EnumLayout out;
  out.layout = Layout{size, align};
  out.disc_type = disc_type_name;
  out.payload_size = payload_size;
  out.payload_align = payload_align;
  return out;
}

std::optional<EnumPayloadMemberLayout> EnumTuplePayloadMemberLayout(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const cursive::ast::VariantDecl& variant,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    std::size_t index) {
  const auto enum_layout =
      EnumLayoutOf(ctx, decl, generic_args, ResolveEnumLayoutOptions(decl.attrs));
  if (!enum_layout.has_value() || !variant.payload_opt.has_value()) {
    return std::nullopt;
  }
  const auto* tuple =
      std::get_if<cursive::ast::VariantPayloadTuple>(&*variant.payload_opt);
  if (!tuple || index >= tuple->elements.size()) {
    return std::nullopt;
  }
  const auto subst = BuildEnumLayoutSubstitution(decl.generic_params, generic_args);
  if (!subst.has_value()) {
    return std::nullopt;
  }

  std::vector<cursive::analysis::TypeRef> field_types;
  field_types.reserve(tuple->elements.size());
  for (const auto& elem : tuple->elements) {
    const auto lowered = LowerEnumPayloadType(ctx, elem, *subst);
    if (!lowered.has_value()) {
      return std::nullopt;
    }
    field_types.push_back(*lowered);
  }
  const auto layout = RecordLayoutOf(ctx, field_types);
  if (!layout.has_value() || index >= layout->offsets.size()) {
    return std::nullopt;
  }

  EnumPayloadMemberLayout out;
  out.type = field_types[index];
  out.offset = layout->offsets[index];
  out.payload_size = enum_layout->payload_size;
  out.payload_align = enum_layout->payload_align;
  return out;
}

std::optional<EnumPayloadMemberLayout> EnumRecordPayloadMemberLayout(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::ast::EnumDecl& decl,
    const cursive::ast::VariantDecl& variant,
    const std::vector<cursive::analysis::TypeRef>& generic_args,
    std::string_view field_name) {
  const auto enum_layout =
      EnumLayoutOf(ctx, decl, generic_args, ResolveEnumLayoutOptions(decl.attrs));
  if (!enum_layout.has_value() || !variant.payload_opt.has_value()) {
    return std::nullopt;
  }
  const auto* record =
      std::get_if<cursive::ast::VariantPayloadRecord>(&*variant.payload_opt);
  if (!record) {
    return std::nullopt;
  }
  const auto subst = BuildEnumLayoutSubstitution(decl.generic_params, generic_args);
  if (!subst.has_value()) {
    return std::nullopt;
  }

  std::vector<cursive::analysis::TypeRef> field_types;
  std::vector<std::string> field_names;
  field_types.reserve(record->fields.size());
  field_names.reserve(record->fields.size());
  for (const auto& field : record->fields) {
    const auto lowered = LowerEnumPayloadType(ctx, field.type, *subst);
    if (!lowered.has_value()) {
      return std::nullopt;
    }
    field_types.push_back(*lowered);
    field_names.push_back(field.name);
  }

  const auto layout = RecordLayoutOf(ctx, field_types);
  if (!layout.has_value()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < field_names.size() && i < layout->offsets.size(); ++i) {
    if (!cursive::analysis::IdEq(field_names[i], std::string(field_name))) {
      continue;
    }
    EnumPayloadMemberLayout out;
    out.type = field_types[i];
    out.offset = layout->offsets[i];
    out.payload_size = enum_layout->payload_size;
    out.payload_align = enum_layout->payload_align;
    return out;
  }

  return std::nullopt;
}

}  // namespace cursive::analysis::layout
