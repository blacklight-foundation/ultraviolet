// =============================================================================
// MIGRATION MAPPING: layout_unions.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.1.4 Union Layout and Discriminants (lines 14632-14835)
//   - Section 6.1.4.1 Niche Optimization (Cursive0) (lines 14634-14827)
//   - NicheSet definitions (lines 14638-14641)
//   - Valid pointer non-zero invariant (lines 14647-14654)
//   - Type Ordering (Cursive0) (lines 14660-14719)
//   - MemberList, MemberIndex, UnionDiscValue (lines 14721-14723)
//   - PayloadMember, NicheApplies predicates (lines 14724-14728)
//   - ValueBits specifications (lines 14729-14770)
//   - Union Layout rules (lines 14773-14815)
//   - Layout-Union-Niche rule (lines 14783-14786)
//   - Layout-Union-Tagged rule (lines 14788-14791)
//   - Size-Union, Align-Union, Layout-Union rules (lines 14793-14806)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/layout/layout_unions.cpp
//   - AlignUp helper (lines 14-23)
//   - DiscTypeName helper (lines 25-36)
//   - DiscTypeLayout helper (lines 38-49)
//   - IsUnitType helper (lines 51-59)
//   - NicheCount function (lines 61-96)
//   - UnionLayoutOf function (lines 100-191)
//
// DEPENDENCIES:
//   - cursive/include/04_analysis/layout/layout.h (UnionLayout struct)
//   - cursive/include/04_analysis/types/type_equiv.h (SortUnionMembers)
//   - cursive/include/04_analysis/types/types.h (TypeRef, TypePerm, TypePtr)
//   - LowerTypeForLayout for type alias resolution
//
// REFACTORING NOTES:
//   1. Union members are sorted using TypeKey ordering (see spec lines 14660-14719)
//   2. Niche optimization applies when:
//      - Exactly one non-unit member with niches (PayloadMember)
//      - All other members are unit type ()
//      - Niche count >= number of members - 1
//   3. NicheSet(Ptr<T>@Valid) = {null pointer bit pattern}
//   4. NicheSet for other types = empty
//   5. Tagged union layout: discriminant + payload
//   6. Niche union layout: payload only (empty members use niche values)
//   7. UnionDiscValue(U, T) = sorted index of T in U
//
// NICHE OPTIMIZATION EXAMPLE:
//   Ptr<i32>@Valid | () uses niche optimization:
//   - Ptr<i32>@Valid stored directly (must be non-null)
//   - () represented as null pointer (0x0)
//   - No discriminant needed
// =============================================================================

#include "04_analysis/layout/layout.h"

#include <algorithm>

#include "00_core/assert_spec.h"
#include "04_analysis/modal/modal_widen.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/types.h"

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

std::optional<Layout> DiscTypeLayout(std::uint64_t max_disc) {
  const auto name = DiscTypeName(max_disc);
  if (!name.has_value()) {
    return std::nullopt;
  }
  const auto size = PrimSize(*name);
  const auto align = PrimAlign(*name);
  if (!size.has_value() || !align.has_value()) {
    return std::nullopt;
  }
  return Layout{*size, *align};
}

bool IsUnitType(const cursive::analysis::TypeRef& type) {
  if (!type) {
    return false;
  }
  if (const auto* prim = std::get_if<cursive::analysis::TypePrim>(&type->node)) {
    return prim->name == "()";
  }
  return false;
}

std::uint64_t NicheCount(const cursive::analysis::ScopeContext& ctx,
                         const cursive::analysis::TypeRef& type) {
  if (!type) {
    return 0;
  }
  if (const auto* perm = std::get_if<cursive::analysis::TypePerm>(&type->node)) {
    return NicheCount(ctx, perm->base);
  }
  if (const auto* ptr = std::get_if<cursive::analysis::TypePtr>(&type->node)) {
    if (ptr->state == cursive::analysis::PtrState::Valid) {
      return 1;
    }
    return 0;
  }
  if (const auto* path =
          std::get_if<cursive::analysis::TypePathType>(&type->node)) {
    const auto it = ctx.sigma.types.find(path->path);
    if (it != ctx.sigma.types.end()) {
      if (const auto* alias =
              std::get_if<cursive::ast::TypeAliasDecl>(&it->second)) {
        const auto lowered = LowerTypeForLayout(ctx, alias->type);
        if (!lowered.has_value()) {
          return 0;
        }
        return NicheCount(ctx, *lowered);
      }
    }
  }
  return 0;
}

}  // namespace

std::optional<UnionLayout> UnionLayoutOf(
    const cursive::analysis::ScopeContext& ctx,
    const cursive::analysis::TypeUnion& uni) {
  if (uni.members.empty()) {
    return std::nullopt;
  }

  const auto member_list = cursive::analysis::SortUnionMembers(uni.members);

  const std::uint64_t required = member_list.empty()
                                    ? 0ull
                                    : static_cast<std::uint64_t>(member_list.size() - 1);

  std::optional<std::size_t> payload_index;
  std::uint64_t niche_count = 0;

  for (std::size_t i = 0; i < member_list.size(); ++i) {
    if (IsUnitType(member_list[i])) {
      continue;
    }
    const auto count = NicheCount(ctx, member_list[i]);
    if (count == 0) {
      continue;
    }
    if (payload_index.has_value()) {
      payload_index.reset();
      break;
    }
    payload_index = i;
    niche_count = count;
  }

  bool niche_applies = payload_index.has_value();
  if (niche_applies && payload_index.has_value()) {
    for (std::size_t i = 0; i < member_list.size(); ++i) {
      if (i == *payload_index) {
        continue;
      }
      if (!IsUnitType(member_list[i])) {
        niche_applies = false;
        break;
      }
    }
    if (niche_count < required) {
      niche_applies = false;
    }
  }

  UnionLayout out;
  out.member_list = member_list;

  if (niche_applies && payload_index.has_value()) {
    SPEC_RULE("Layout-Union-Niche");
    const auto payload_layout = LayoutOf(ctx, member_list[*payload_index]);
    if (!payload_layout.has_value()) {
      return std::nullopt;
    }
    out.niche = true;
    out.layout = *payload_layout;
    out.niche_payload_layout = *payload_layout;
    out.payload_size = payload_layout->size;
    out.payload_align = payload_layout->align;
    return out;
  }

  SPEC_RULE("Layout-Union-Tagged");
  std::uint64_t payload_size = 0;
  std::uint64_t payload_align = 1;
  for (const auto& member : member_list) {
    const auto member_layout = LayoutOf(ctx, member);
    if (!member_layout.has_value()) {
      return std::nullopt;
    }
    payload_size = std::max(payload_size, member_layout->size);
    payload_align = std::max(payload_align, member_layout->align);
  }

  const auto disc_layout = DiscTypeLayout(member_list.size() - 1);
  if (!disc_layout.has_value()) {
    return std::nullopt;
  }

  const std::uint64_t align = std::max(disc_layout->align, payload_align);
  const std::uint64_t size = AlignUp(disc_layout->size + payload_size, align);
  out.niche = false;
  out.layout = Layout{size, align};
  out.payload_size = payload_size;
  out.payload_align = payload_align;
  out.disc_type = DiscTypeName(member_list.size() - 1);
  return out;
}

}  // namespace cursive::analysis::layout
