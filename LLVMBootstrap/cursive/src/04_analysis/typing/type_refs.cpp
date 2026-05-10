// =============================================================================
// MIGRATION MAPPING: type_refs.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   Section 3.3.2.3: Type Representation (referenced in types.cpp SPEC_DEFs)
//   Section 6.1.4.1: Type Key definitions for ordering
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/types/types.cpp
//   Lines 1-1017 (complete file)
//
// KEY CONTENT TO MIGRATE:
//   - SpecDefsPermissionSets() for PermSet_C0, S_Perms (lines 15-18)
//   - SpecDefsTypeRepr() for Type, TypeCtor, etc. (lines 20-31)
//   - SpecDefsTypePaths() for TypePaths definition (lines 33-35)
//   - SpecDefsTypeKey() for key definitions (lines 37-52)
//
//   TYPE CONSTRUCTION FUNCTIONS:
//   - MakeType() - base type constructor (lines 359-362)
//   - MakeTypePrim() - primitive type (lines 364-367)
//   - MakeTypeRange*() - range family constructors (lines 369-372)
//   - MakeTypePerm() - permission-qualified type (lines 374-377)
//   - MakeTypeUnion() - union type (lines 379-382)
//   - MakeTypeFunc() - function type (lines 384-387)
//   - MakeTypeTuple() - tuple type (lines 389-392)
//   - MakeTypeArray() - array type with length (lines 394-397)
//   - MakeTypeSlice() - slice type (lines 399-402)
//   - MakeTypePtr() - safe pointer type with state (lines 404-407)
//   - MakeTypeRawPtr() - raw pointer type (lines 409-412)
//   - MakeTypeString() - string type with state (lines 414-417)
//   - MakeTypeBytes() - bytes type with state (lines 419-422)
//   - MakeTypeDynamic() - dynamic class type (lines 424-427)
//   - MakeTypeModalState() - modal state type (lines 429-434)
//   - MakeTypePath() - named type path (lines 436-444)
//   - MakeTypeOpaque() - opaque type (lines 446-451)
//   - MakeTypeRefine() - refinement type (lines 453-457)
//
//   TYPE STRING CONVERSION:
//   - AppendTypeString() - internal string builder (lines 459-575)
//   - TypeToString() overloads (lines 577-590)
//
//   TYPE KEY SYSTEM (for ordering):
//   - KeyAtom class methods (lines 592-618)
//   - TagKeyOf() - variant tag key (lines 89-132)
//   - PermKeyOf() - permission key (lines 134-146)
//   - PtrStateKeyOf() - pointer state key (lines 148-161)
//   - QualKeyOf() - raw pointer qualifier key (lines 163-165)
//   - ModeKeyOf() - parameter mode key (lines 167-172)
//   - StateKeyOf() - string/bytes state key (lines 174-186)
//   - TypeKeyOf() - complete type key computation (lines 620-768)
//   - TypeKeyEqual() - key equality (lines 770-781)
//   - TypeKeyLess() - key ordering (lines 783-786)
//   - SortUnionMembers() - canonical union ordering (lines 788-809)
//
//   TYPE PATH COLLECTION:
//   - CollectTypePaths() - extract all paths from type (lines 811-865)
//   - TypePaths() overloads (lines 867-882)
//
//   ASYNC TYPE HELPERS:
//   - IdEq() - identifier equality (lines 886-888)
//   - IsAsyncPath() - check for Async type (lines 893-895)
//   - IsAsyncAliasPath() - check for async aliases (lines 898-903)
//   - IsAsyncType() - determine if type is async (lines 907-927)
//   - GetAsyncSig() - extract async signature (lines 929-1014)
//     * Handles: Async<Out,In,Result,E>, Future<T;E>, Sequence<T>,
//                Stream<T;E>, Pipe<In;Out>, Exchange<T>
//
//   PERMISSION UTILITIES:
//   - IsPermInC0() - permission set membership (lines 337-343)
//   - ParsePermissionKeyword() - keyword to permission (lines 345-357)
//
// DEPENDENCIES:
//   - core::Span for source locations
//   - ast::Expr, ast::Type for AST references
//   - project::Fold, project::Utf8LexLess for deterministic ordering
//
// REFACTORING NOTES:
//   1. Type construction functions are factory functions - consider builder pattern
//   2. TypeKey system is for deterministic ordering (important for union normalization)
//   3. Async signature extraction is complex - could be its own module
//   4. PathToString and FoldPath are used for key generation
//   5. SortUnionMembers is critical for type equivalence (unions are unordered)
//   6. Consider separating into:
//      - type_construct.cpp (MakeType* functions)
//      - type_key.cpp (TypeKey system)
//      - type_async.cpp (async helpers)
//
// TYPE NODE VARIANTS:
//   TypePrim, TypeTuple, TypeArray, TypeSlice, TypeFunc, TypePathType,
//   TypeModalState, TypeString, TypeBytes, TypeDynamic, TypePtr, TypeRawPtr,
//   TypeUnion, TypePerm, TypeRange*, TypeOpaque, TypeRefine
//
// =============================================================================
#include "04_analysis/typing/types.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "00_core/assert_spec.h"
#include "01_project/deterministic_order.h"
#include "02_source/ast/ast.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/generics/monomorphize.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/type_equiv.h"
#include "04_analysis/typing/type_stmt.h"
#include "04_analysis/typing/type_lower.h"

namespace cursive::analysis {

static void AppendTypeString(std::string& out, const Type& type);
static TypeKey ComputeTypeKey(const Type& type);

namespace {

static const ast::TypeAliasDecl* LookupTypeAliasDecl(const ScopeContext& ctx,
                                                     const TypePath& path) {
  ast::Path syntax_path;
  syntax_path.reserve(path.size());
  for (const auto& segment : path) {
    syntax_path.push_back(segment);
  }

  const auto it = ctx.sigma.types.find(PathKeyOf(syntax_path));
  if (it != ctx.sigma.types.end()) {
    return std::get_if<ast::TypeAliasDecl>(&it->second);
  }

  if (path.size() != 1) {
    return nullptr;
  }

  const auto ent = ResolveTypeName(ctx, path[0]);
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return nullptr;
  }

  ast::Path resolved = *ent->origin_opt;
  const std::string resolved_name =
      ent->target_opt.has_value() ? *ent->target_opt : path[0];
  resolved.push_back(resolved_name);

  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return nullptr;
  }

  return std::get_if<ast::TypeAliasDecl>(&resolved_it->second);
}

static inline void SpecDefsPermissionSets() {
  SPEC_DEF("PermSet_C0", "1.1.1");
  SPEC_DEF("S_Perms", "1.1.1");
}

static inline void SpecDefsTypeRepr() {
  SPEC_DEF("Type", "3.3.2.3");
  SPEC_DEF("TypeCtor", "1.1.1");
  SPEC_DEF("TypeConstructs", "1.1.1");
  SPEC_DEF("TypeRangeSyntax", "3.3.2.3");
  SPEC_DEF("ParamMode", "3.3.2.3");
  SPEC_DEF("Perm", "3.3.2.3");
  SPEC_DEF("Qual", "3.3.2.3");
  SPEC_DEF("PtrStateOpt", "3.3.2.3");
  SPEC_DEF("StringStateOpt", "3.3.2.3");
  SPEC_DEF("BytesStateOpt", "3.3.2.3");
}

static inline void SpecDefsTypePaths() {
  SPEC_DEF("TypePaths", "3.3.2.2");
}

static inline void SpecDefsTypeKey() {
  SPEC_DEF("PathOrderKey", "6.1.4.1");
  SPEC_DEF("ArrayLen", "6.1.4.1");
  SPEC_DEF("TagKey", "6.1.4.1");
  SPEC_DEF("PermKey", "6.1.4.1");
  SPEC_DEF("PtrStateKey", "6.1.4.1");
  SPEC_DEF("QualKey", "6.1.4.1");
  SPEC_DEF("ModeKey", "6.1.4.1");
  SPEC_DEF("StateKey", "6.1.4.1");
  SPEC_DEF("TypeKey", "6.1.4.1");
  SPEC_DEF("Key", "6.1.4.1");
  SPEC_DEF("KeyList", "6.1.4.1");
  SPEC_DEF("Sort", "6.1.4.1");
  SPEC_DEF("Sorted", "6.1.4.1");
  SPEC_DEF("LexLess", "6.1.4.1");
}

static std::string PathToString(const TypePath& path) {
  if (path.empty()) {
    return "";
  }
  std::string out = path[0];
  for (std::size_t i = 1; i < path.size(); ++i) {
    out.append("::");
    out.append(path[i]);
  }
  return out;
}

static std::string FoldPath(const TypePath& path) {
  return project::Fold(PathToString(path));
}

static void AppendModalRefString(std::string& out, const ModalRef& modal_ref) {
  out.append(PathToString(ModalRefPath(modal_ref)));
  const auto& args = ModalRefArgs(modal_ref);
  if (args.empty()) {
    return;
  }

  out.push_back('<');
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0) {
      out.append(", ");
    }
    AppendTypeString(out, *args[i]);
  }
  out.push_back('>');
}

static bool PathLess(const TypePath& lhs, const TypePath& rhs) {
  const std::string lhs_fold = FoldPath(lhs);
  const std::string rhs_fold = FoldPath(rhs);
  if (project::Utf8LexLess(lhs_fold, rhs_fold)) {
    return true;
  }
  if (lhs_fold == rhs_fold) {
    return project::Utf8LexLess(PathToString(lhs), PathToString(rhs));
  }
  return false;
}

static TypeKey PathOrderKey(const TypePath& path) {
  TypeKey key;
  key.atoms.push_back(KeyAtom::String(FoldPath(path)));
  key.atoms.push_back(KeyAtom::String(PathToString(path)));
  return key;
}

static std::uint64_t TagKeyOf(const TypeNode& node) {
  return std::visit(
      [](const auto& value) -> std::uint64_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return 0;
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          return 18;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          return 1;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return 2;
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return 3;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          return 4;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          return 5;
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          return 5;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          return 6;
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return 7;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return 8;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          return 9;
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return 10;
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          return 11;
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          return 12;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          return 13;
        } else if constexpr (std::is_same_v<T, TypeRange> ||
                             std::is_same_v<T, TypeRangeInclusive> ||
                             std::is_same_v<T, TypeRangeFrom> ||
                             std::is_same_v<T, TypeRangeTo> ||
                             std::is_same_v<T, TypeRangeToInclusive> ||
                             std::is_same_v<T, TypeRangeFull>) {
          return 14;
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          return 15;
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return 16;
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          return 17;
        } else {
          return 0;
        }
      },
      node);
}

static std::uint64_t PermKeyOf(Permission perm) {
  // Stable ordering for structural type keys.
  if (perm == Permission::Const) {
    return 0;
  }
  if (perm == Permission::Unique) {
    return 1;
  }
  // Permission::Shared
  SPEC_RULE("Perm-Shared");
  return 2;
}

static std::uint64_t PtrStateKeyOf(const std::optional<PtrState>& state) {
  if (!state.has_value()) {
    return 0;
  }
  switch (*state) {
    case PtrState::Valid:
      return 1;
    case PtrState::Null:
      return 2;
    case PtrState::Expired:
      return 3;
  }
  return 0;
}

static std::uint64_t QualKeyOf(RawPtrQual qual) {
  return qual == RawPtrQual::Imm ? 0 : 1;
}

static std::uint64_t ModeKeyOf(const std::optional<ParamMode>& mode) {
  if (!mode.has_value()) {
    return 0;
  }
  return 1;
}

template <typename State>
static std::uint64_t StateKeyOf(const std::optional<State>& state) {
  if (!state.has_value()) {
    return 2;
  }
  switch (*state) {
    case State::View:
      return 0;
    case State::Managed:
      return 1;
  }
  return 2;
}

static bool TypeKeyLessImpl(const TypeKey& lhs, const TypeKey& rhs);

static bool KeyAtomEqual(const KeyAtom& lhs, const KeyAtom& rhs) {
  if (lhs.kind != rhs.kind) {
    return false;
  }
  switch (lhs.kind) {
    case KeyAtom::Kind::Number:
      return lhs.number == rhs.number;
    case KeyAtom::Kind::String:
      return lhs.text == rhs.text;
    case KeyAtom::Kind::Key:
      if (!lhs.key || !rhs.key) {
        return lhs.key == rhs.key;
      }
      return !TypeKeyLessImpl(*lhs.key, *rhs.key) &&
             !TypeKeyLessImpl(*rhs.key, *lhs.key);
    case KeyAtom::Kind::KeyList:
      if (lhs.key_list.size() != rhs.key_list.size()) {
        return false;
      }
      for (std::size_t i = 0; i < lhs.key_list.size(); ++i) {
        const auto& l_key = lhs.key_list[i];
        const auto& r_key = rhs.key_list[i];
        if (l_key && r_key) {
          if (TypeKeyLessImpl(*l_key, *r_key) ||
              TypeKeyLessImpl(*r_key, *l_key)) {
            return false;
          }
        } else if (l_key || r_key) {
          return false;
        }
      }
      return true;
  }
  return false;
}

static bool KeyListLess(const std::vector<std::shared_ptr<TypeKey>>& lhs,
                        const std::vector<std::shared_ptr<TypeKey>>& rhs) {
  const std::size_t count = std::min(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto& l_key = lhs[i];
    const auto& r_key = rhs[i];
    if (l_key && r_key) {
      if (TypeKeyLessImpl(*l_key, *r_key)) {
        return true;
      }
      if (TypeKeyLessImpl(*r_key, *l_key)) {
        return false;
      }
    } else if (!l_key && r_key) {
      return true;
    } else if (l_key && !r_key) {
      return false;
    }
  }
  return lhs.size() < rhs.size();
}

static bool KeyAtomLess(const KeyAtom& lhs, const KeyAtom& rhs) {
  if (lhs.kind != rhs.kind) {
    return static_cast<int>(lhs.kind) < static_cast<int>(rhs.kind);
  }
  switch (lhs.kind) {
    case KeyAtom::Kind::Number:
      return lhs.number < rhs.number;
    case KeyAtom::Kind::String:
      return project::Utf8LexLess(lhs.text, rhs.text);
    case KeyAtom::Kind::Key:
      if (!lhs.key || !rhs.key) {
        return !lhs.key && rhs.key;
      }
      return TypeKeyLessImpl(*lhs.key, *rhs.key);
    case KeyAtom::Kind::KeyList:
      return KeyListLess(lhs.key_list, rhs.key_list);
  }
  return false;
}

static bool TypeKeyLessImpl(const TypeKey& lhs, const TypeKey& rhs) {
  const std::size_t count = std::min(lhs.atoms.size(), rhs.atoms.size());
  for (std::size_t i = 0; i < count; ++i) {
    if (KeyAtomLess(lhs.atoms[i], rhs.atoms[i])) {
      return true;
    }
    if (KeyAtomLess(rhs.atoms[i], lhs.atoms[i])) {
      return false;
    }
  }
  return lhs.atoms.size() < rhs.atoms.size();
}

static std::vector<std::shared_ptr<TypeKey>> MakeKeyList(
    const std::vector<TypeKey>& keys) {
  std::vector<std::shared_ptr<TypeKey>> out;
  out.reserve(keys.size());
  for (const auto& key : keys) {
    out.push_back(std::make_shared<TypeKey>(key));
  }
  return out;
}

static std::string PermString(Permission perm) {
  switch (perm) {
    case Permission::Const:
      return "const";
    case Permission::Unique:
      return "unique";
    case Permission::Shared:
      return "shared";
  }
  return "const";
}

static std::string PtrStateString(PtrState state) {
  switch (state) {
    case PtrState::Valid:
      return "@Valid";
    case PtrState::Null:
      return "@Null";
    case PtrState::Expired:
      return "@Expired";
  }
  return "";
}

static std::string StringStateString(StringState state) {
  switch (state) {
    case StringState::Managed:
      return "@Managed";
    case StringState::View:
      return "@View";
  }
  return "";
}

static std::string BytesStateString(BytesState state) {
  switch (state) {
    case BytesState::Managed:
      return "@Managed";
    case BytesState::View:
      return "@View";
  }
  return "";
}

}  // namespace

bool IsPermInC0(Permission perm) {
  SpecDefsPermissionSets();
  (void)perm;
  return true;
}

std::optional<Permission> ParsePermissionKeyword(std::string_view token) {
  SpecDefsPermissionSets();
  if (token == "const") {
    return Permission::Const;
  }
  if (token == "unique") {
    return Permission::Unique;
  }
  if (token == "shared") {
    return Permission::Shared;
  }
  return std::nullopt;
}

TypeRef MakeType(TypeNode node) {
  SpecDefsTypeRepr();
  auto candidate = std::make_shared<Type>();
  candidate->node = std::move(node);
  return candidate;
}

ModalRef MakeModalRef(TypePath path, std::vector<TypeRef> args) {
  SpecDefsTypeRepr();
  if (args.empty()) {
    SPEC_RULE("TypeRef-TypePath");
    return ModalRef{std::move(path)};
  }

  SPEC_RULE("TypeRef-TypeApply");
  return ModalRef{TypeApply{std::move(path), std::move(args)}};
}

const TypePath& ModalRefPath(const ModalRef& modal_ref) {
  SpecDefsTypeRepr();
  return std::visit(
      [](const auto& node) -> const TypePath& {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePath>) {
          SPEC_RULE("ModalRefPath(TypePath(p))");
          return node;
        } else {
          SPEC_RULE("ModalRefPath(TypeApply(p, _))");
          return node.path;
        }
      },
      modal_ref);
}

const std::vector<TypeRef>& ModalRefArgs(const ModalRef& modal_ref) {
  SpecDefsTypeRepr();
  static const std::vector<TypeRef> kEmptyArgs;
  return std::visit(
      [](const auto& node) -> const std::vector<TypeRef>& {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePath>) {
          SPEC_RULE("ModalRefArgs(TypePath(_))");
          return kEmptyArgs;
        } else {
          SPEC_RULE("ModalRefArgs(TypeApply(_, args))");
          return node.args;
        }
      },
      modal_ref);
}

TypeRef ModalRefType(const ModalRef& modal_ref) {
  SpecDefsTypeRepr();
  return std::visit(
      [](const auto& node) -> TypeRef {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePath>) {
          SPEC_RULE("ModalRefType(TypePath(p))");
          return MakeTypePath(node);
        } else {
          SPEC_RULE("ModalRefType(TypeApply(p, args))");
          if (core::Conformance::Enabled()) {
            core::Conformance::Record("ModalRefType(TypeApply(p, args))",
                                      std::nullopt, "result=TypeApply");
          }
          return MakeTypeApply(node.path, node.args);
        }
      },
      modal_ref);
}

TypeRef MakeTypePrim(std::string name) {
  SpecDefsTypeRepr();
  return MakeType(TypePrim{std::move(name)});
}

TypeRef MakeTypeVar(std::uint32_t id) {
  SpecDefsTypeRepr();
  return MakeType(TypeVar{id});
}

TypeRef MakeTypeRange(TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypeRange{std::move(base)});
}

TypeRef MakeTypeRangeInclusive(TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypeRangeInclusive{std::move(base)});
}

TypeRef MakeTypeRangeFrom(TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypeRangeFrom{std::move(base)});
}

TypeRef MakeTypeRangeTo(TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypeRangeTo{std::move(base)});
}

TypeRef MakeTypeRangeToInclusive(TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypeRangeToInclusive{std::move(base)});
}

TypeRef MakeTypeRangeFull() {
  SpecDefsTypeRepr();
  return MakeType(TypeRangeFull{});
}

TypeRef MakeTypePerm(Permission perm, TypeRef base) {
  SpecDefsTypeRepr();
  return MakeType(TypePerm{perm, std::move(base)});
}

namespace {

void CollectUnionMembers(const TypeRef& member, std::vector<TypeRef>& out) {
  if (!member) {
    return;
  }
  if (const auto* uni = std::get_if<TypeUnion>(&member->node)) {
    for (const auto& nested : uni->members) {
      CollectUnionMembers(nested, out);
    }
    return;
  }
  out.push_back(member);
}

bool ContainsEquivalentUnionMember(const std::vector<TypeRef>& members,
                                   const TypeRef& candidate) {
  if (!candidate) {
    return true;
  }
  for (const auto& existing : members) {
    if (!existing) {
      continue;
    }
    const auto equiv = TypeEquiv(existing, candidate);
    if (equiv.ok && equiv.equiv) {
      return true;
    }
  }
  return false;
}

}  // namespace

TypeRef MakeTypeUnion(std::vector<TypeRef> members) {
  SpecDefsTypeRepr();
  std::vector<TypeRef> flat;
  flat.reserve(members.size());
  for (const auto& member : members) {
    CollectUnionMembers(member, flat);
  }

  if (flat.empty()) {
    return MakeType(TypeUnion{std::move(flat)});
  }

  std::vector<TypeRef> sorted = SortUnionMembers(flat);

  std::vector<TypeRef> distinct;
  distinct.reserve(sorted.size());
  for (const auto& member : sorted) {
    if (!member) {
      continue;
    }
    if (!ContainsEquivalentUnionMember(distinct, member)) {
      distinct.push_back(member);
    }
  }

  if (distinct.size() == 1) {
    return distinct.front();
  }
  return MakeType(TypeUnion{std::move(distinct)});
}

TypeRef MakeTypeFunc(std::vector<TypeFuncParam> params, TypeRef ret) {
  SpecDefsTypeRepr();
  return MakeType(TypeFunc{std::move(params), std::move(ret)});
}

TypeRef MakeTypeTuple(std::vector<TypeRef> elements) {
  SpecDefsTypeRepr();
  return MakeType(TypeTuple{std::move(elements)});
}

TypeRef MakeTypeArray(TypeRef element,
                      std::uint64_t length,
                      std::optional<std::string> length_expr_text) {
  SpecDefsTypeRepr();
  return MakeType(
      TypeArray{std::move(element), length, std::move(length_expr_text)});
}

TypeRef MakeTypeSlice(TypeRef element) {
  SpecDefsTypeRepr();
  return MakeType(TypeSlice{std::move(element)});
}

TypeRef MakeTypePtr(TypeRef element, std::optional<PtrState> state) {
  SpecDefsTypeRepr();
  return MakeType(TypePtr{std::move(element), state});
}

TypeRef MakeTypeRawPtr(RawPtrQual qual, TypeRef element) {
  SpecDefsTypeRepr();
  return MakeType(TypeRawPtr{qual, std::move(element)});
}

TypeRef MakeTypeString(std::optional<StringState> state) {
  SpecDefsTypeRepr();
  return MakeType(TypeString{state});
}

TypeRef MakeTypeBytes(std::optional<BytesState> state) {
  SpecDefsTypeRepr();
  return MakeType(TypeBytes{state});
}

TypeRef MakeTypeDynamic(TypePath path) {
  SpecDefsTypeRepr();
  return MakeType(TypeDynamic{std::move(path)});
}

TypeRef MakeTypeModalState(ModalRef modal_ref, std::string state) {
  SpecDefsTypeRepr();
  SPEC_RULE("TypeRef-ModalStateRef");
  TypeModalState node;
  node.modal_ref = std::move(modal_ref);
  node.state = std::move(state);
  SyncTypeModalStateFromModalRef(node);
  return MakeType(std::move(node));
}

TypeRef MakeTypeModalState(TypePath path,
                           std::string state,
                           std::vector<TypeRef> generic_args) {
  SpecDefsTypeRepr();
  return MakeTypeModalState(MakeModalRef(std::move(path), std::move(generic_args)),
                            std::move(state));
}

TypeRef MakeTypePath(TypePath path) {
  SpecDefsTypeRepr();
  return MakeType(TypePathType{std::move(path), {}});
}

TypeRef MakeTypePath(TypePath path, std::vector<TypeRef> generic_args) {
  SpecDefsTypeRepr();
  return MakeType(TypePathType{std::move(path), std::move(generic_args)});
}

TypePath SelfVarPath() {
  SPEC_RULE("SelfVar");
  return TypePath{"Self"};
}

TypeRef SelfVarType() {
  return MakeTypePath(SelfVarPath());
}

bool IsSelfVarPath(const TypePath& path) {
  return path.size() == 1 && path.front() == "Self";
}

TypeRef MakeTypeApply(TypePath path, std::vector<TypeRef> args) {
  SpecDefsTypeRepr();
  SPEC_RULE("TypeRef-TypeApply");
  return MakeType(TypeApply{std::move(path), std::move(args)});
}

TypeRef MakeTypeOpaque(TypePath class_path,
                       const ast::Type* origin,
                       const core::Span& origin_span) {
  SpecDefsTypeRepr();
  return MakeType(TypeOpaque{std::move(class_path), origin, origin_span});
}

TypeRef MakeTypeRefine(TypeRef base,
                       std::shared_ptr<ast::Expr> predicate) {
  SpecDefsTypeRepr();
  return MakeType(TypeRefine{std::move(base), std::move(predicate)});
}

TypeRef MakeTypeClosure(std::vector<std::pair<bool, TypeRef>> params,
                        TypeRef ret,
                        std::optional<std::vector<SharedDep>> deps_opt) {
  SpecDefsTypeRepr();
  return MakeType(TypeClosure{std::move(params), std::move(ret), std::move(deps_opt)});
}

static void AppendTypeString(std::string& out, const Type& type) {
  auto append_generic_args = [&](const std::vector<TypeRef>& args) {
    if (args.empty()) {
      return;
    }
    out.push_back('<');
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (i != 0) {
        out.append(", ");
      }
      AppendTypeString(out, *args[i]);
    }
    out.push_back('>');
  };

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          out.append(node.name);
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          out.append("$t");
          out.append(std::to_string(node.id));
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          out.append("Range<");
          if (node.base) {
            AppendTypeString(out, *node.base);
          }
          out.push_back('>');
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          out.append("RangeInclusive<");
          if (node.base) {
            AppendTypeString(out, *node.base);
          }
          out.push_back('>');
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          out.append("RangeFrom<");
          if (node.base) {
            AppendTypeString(out, *node.base);
          }
          out.push_back('>');
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          out.append("RangeTo<");
          if (node.base) {
            AppendTypeString(out, *node.base);
          }
          out.push_back('>');
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          out.append("RangeToInclusive<");
          if (node.base) {
            AppendTypeString(out, *node.base);
          }
          out.push_back('>');
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          out.append("RangeFull");
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          out.append(PermString(node.perm));
          out.push_back(' ');
          AppendTypeString(out, *node.base);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (std::size_t i = 0; i < node.members.size(); ++i) {
            if (i > 0) {
              out.append(" | ");
            }
            AppendTypeString(out, *node.members[i]);
          }
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          out.push_back('(');
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (i > 0) {
              out.append(", ");
            }
            const auto& param = node.params[i];
            if (param.mode.has_value() && *param.mode == ParamMode::Move) {
              out.append("move ");
            }
            AppendTypeString(out, *param.type);
          }
          out.append(") -> ");
          AppendTypeString(out, *node.ret);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          if (node.elements.empty()) {
            out.append("()");
            return;
          }
          out.push_back('(');
          if (node.elements.size() == 1) {
            AppendTypeString(out, *node.elements[0]);
            out.append(";)");
            return;
          }
          for (std::size_t i = 0; i < node.elements.size(); ++i) {
            if (i > 0) {
              out.append(", ");
            }
            AppendTypeString(out, *node.elements[i]);
          }
          out.push_back(')');
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          out.push_back('[');
          AppendTypeString(out, *node.element);
          out.append("; ");
          if (node.length_expr_text.has_value() &&
              !node.length_expr_text->empty()) {
            out.append(*node.length_expr_text);
          } else {
            out.append(std::to_string(node.length));
          }
          out.push_back(']');
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          out.push_back('[');
          AppendTypeString(out, *node.element);
          out.push_back(']');
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          out.append("Ptr<");
          AppendTypeString(out, *node.element);
          out.push_back('>');
          if (node.state.has_value()) {
            out.append(PtrStateString(*node.state));
          }
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          out.append("* ");
          out.append(node.qual == RawPtrQual::Imm ? "imm " : "mut ");
          AppendTypeString(out, *node.element);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          out.append("string");
          if (node.state.has_value()) {
            out.append(StringStateString(*node.state));
          }
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          out.append("bytes");
          if (node.state.has_value()) {
            out.append(BytesStateString(*node.state));
          }
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          out.push_back('$');
          out.append(PathToString(node.path));
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          AppendModalRefString(out, node.modal_ref);
          out.push_back('@');
          out.append(node.state);
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          out.append("opaque ");
          out.append(PathToString(node.class_path));
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          AppendTypeString(out, *node.base);
          out.append(" where { ... }");
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          out.push_back('|');
          for (std::size_t i = 0; i < node.params.size(); ++i) {
            if (i != 0) {
              out.append(", ");
            }
            if (node.params[i].first) {
              out.append("move ");
            }
            if (node.params[i].second) {
              AppendTypeString(out, *node.params[i].second);
            }
          }
          out.append("| -> ");
          if (node.ret) {
            AppendTypeString(out, *node.ret);
          } else {
            out.append("()");
          }
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          out.append(PathToString(node.path));
          append_generic_args(node.generic_args);
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          out.append(PathToString(node.path));
          append_generic_args(node.args);
        }
      },
      type.node);
}

std::string TypeToString(const Type& type) {
  SpecDefsTypeRepr();
  std::string out;
  AppendTypeString(out, type);
  return out;
}

std::string TypeToString(const TypeRef& type) {
  SpecDefsTypeRepr();
  if (!type) {
    return "";
  }
  return TypeToString(*type);
}

KeyAtom KeyAtom::Number(std::uint64_t value) {
  KeyAtom atom;
  atom.kind = Kind::Number;
  atom.number = value;
  return atom;
}

KeyAtom KeyAtom::String(std::string value) {
  KeyAtom atom;
  atom.kind = Kind::String;
  atom.text = std::move(value);
  return atom;
}

KeyAtom KeyAtom::Key(std::shared_ptr<TypeKey> value) {
  KeyAtom atom;
  atom.kind = Kind::Key;
  atom.key = std::move(value);
  return atom;
}

KeyAtom KeyAtom::KeyList(std::vector<std::shared_ptr<TypeKey>> value) {
  KeyAtom atom;
  atom.kind = Kind::KeyList;
  atom.key_list = std::move(value);
  return atom;
}

static TypeKey ComputeTypeKey(const Type& type) {
  SpecDefsTypeKey();
  return std::visit(
      [&](const auto& node) -> TypeKey {
        using T = std::decay_t<decltype(node)>;
        TypeKey key;
        if constexpr (std::is_same_v<T, TypePrim>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::String(node.name));
          return key;
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(node.id));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(0));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(1));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(2));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(3));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(4));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(5));
          return key;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(node.elements.size()));
          for (const auto& elem : node.elements) {
            key.atoms.push_back(
                KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(elem))));
          }
          return key;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.element))));
          key.atoms.push_back(KeyAtom::Number(node.length));
          return key;
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.element))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(node.params.size()));
          for (const auto& param : node.params) {
            key.atoms.push_back(KeyAtom::Number(ModeKeyOf(param.mode)));
            key.atoms.push_back(KeyAtom::Key(
                std::make_shared<TypeKey>(TypeKeyOf(param.type))));
          }
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.ret))));
          return key;
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(PathOrderKey(node.path))));
          if (!node.generic_args.empty()) {
            std::vector<TypeKey> arg_keys;
            arg_keys.reserve(node.generic_args.size());
            for (const auto& arg : node.generic_args) {
              arg_keys.push_back(TypeKeyOf(arg));
            }
            key.atoms.push_back(KeyAtom::KeyList(MakeKeyList(arg_keys)));
          }
          return key;
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(PathOrderKey(node.path))));
          std::vector<TypeKey> arg_keys;
          arg_keys.reserve(node.args.size());
          for (const auto& arg : node.args) {
            arg_keys.push_back(TypeKeyOf(arg));
          }
          key.atoms.push_back(KeyAtom::KeyList(MakeKeyList(arg_keys)));
          return key;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          const auto& modal_path = ModalRefPath(node.modal_ref);
          const auto& modal_args = ModalRefArgs(node.modal_ref);
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(PathOrderKey(modal_path))));
          if (!modal_args.empty()) {
            std::vector<TypeKey> arg_keys;
            arg_keys.reserve(modal_args.size());
            for (const auto& arg : modal_args) {
              arg_keys.push_back(TypeKeyOf(arg));
            }
            key.atoms.push_back(KeyAtom::KeyList(MakeKeyList(arg_keys)));
          }
          key.atoms.push_back(KeyAtom::String(node.state));
          return key;
        } else if constexpr (std::is_same_v<T, TypeString>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Number(StateKeyOf<StringState>(node.state)));
          return key;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Number(StateKeyOf<BytesState>(node.state)));
          return key;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(PathOrderKey(node.path))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Key(
              std::make_shared<TypeKey>(PathOrderKey(node.class_path))));
          key.atoms.push_back(KeyAtom::String(node.origin_span.file));
          key.atoms.push_back(KeyAtom::Number(node.origin_span.start_offset));
          key.atoms.push_back(KeyAtom::Number(node.origin_span.end_offset));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          if (node.predicate) {
            key.atoms.push_back(KeyAtom::String(node.predicate->span.file));
            key.atoms.push_back(
                KeyAtom::Number(node.predicate->span.start_offset));
            key.atoms.push_back(
                KeyAtom::Number(node.predicate->span.end_offset));
          }
          return key;
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(PtrStateKeyOf(node.state)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.element))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(QualKeyOf(node.qual)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.element))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          std::vector<TypeKey> member_keys;
          member_keys.reserve(node.members.size());
          for (const auto& member : node.members) {
            member_keys.push_back(TypeKeyOf(member));
          }
          std::stable_sort(member_keys.begin(), member_keys.end(),
                           [](const TypeKey& lhs, const TypeKey& rhs) {
                             return TypeKeyLessImpl(lhs, rhs);
                           });
          key.atoms.push_back(KeyAtom::KeyList(MakeKeyList(member_keys)));
          return key;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          key.atoms.push_back(KeyAtom::Number(PermKeyOf(node.perm)));
          key.atoms.push_back(
              KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.base))));
          return key;
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          std::vector<std::shared_ptr<TypeKey>> param_keys;
          for (const auto& [is_move, param_type] : node.params) {
            param_keys.push_back(
                std::make_shared<TypeKey>(TypeKey{{KeyAtom::Number(is_move ? 1 : 0)}}));
            if (param_type) {
              param_keys.push_back(
                  std::make_shared<TypeKey>(TypeKeyOf(param_type)));
            }
          }
          key.atoms.push_back(KeyAtom::KeyList(param_keys));
          if (node.ret) {
            key.atoms.push_back(
                KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(node.ret))));
          }
          key.atoms.push_back(KeyAtom::Number(node.deps_opt.has_value() ? 1 : 0));
          if (node.deps_opt.has_value()) {
            std::vector<TypeKey> dep_keys;
            dep_keys.reserve(node.deps_opt->size());
            for (const auto& dep : *node.deps_opt) {
              TypeKey dep_key;
              dep_key.atoms.push_back(KeyAtom::String(dep.name));
              dep_key.atoms.push_back(
                  KeyAtom::Key(std::make_shared<TypeKey>(TypeKeyOf(dep.type))));
              dep_keys.push_back(std::move(dep_key));
            }
            key.atoms.push_back(KeyAtom::KeyList(MakeKeyList(dep_keys)));
          }
          return key;
        } else {
          key.atoms.push_back(KeyAtom::Number(TagKeyOf(type.node)));
          return key;
        }
      },
      type.node);
}

TypeKey TypeKeyOf(const Type& type) {
  SpecDefsTypeKey();
  return ComputeTypeKey(type);
}

TypeKey TypeKeyOf(const TypeRef& type) {
  SpecDefsTypeKey();
  if (!type) {
    return TypeKey{};
  }
  return TypeKeyOf(*type);
}

bool TypeKeyEqual(const TypeKey& lhs, const TypeKey& rhs) {
  SpecDefsTypeKey();
  if (lhs.atoms.size() != rhs.atoms.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.atoms.size(); ++i) {
    if (!KeyAtomEqual(lhs.atoms[i], rhs.atoms[i])) {
      return false;
    }
  }
  return true;
}

bool TypeKeyLess(const TypeKey& lhs, const TypeKey& rhs) {
  SpecDefsTypeKey();
  return TypeKeyLessImpl(lhs, rhs);
}

std::vector<TypeRef> SortUnionMembers(const std::vector<TypeRef>& members) {
  SpecDefsTypeKey();
  struct Entry {
    TypeRef member;
    TypeKey key;
  };
  std::vector<Entry> entries;
  entries.reserve(members.size());
  for (const auto& member : members) {
    if (!member) {
      continue;
    }
    entries.push_back(Entry{member, TypeKeyOf(member)});
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& lhs, const Entry& rhs) {
                     return TypeKeyLessImpl(lhs.key, rhs.key);
                   });
  std::vector<TypeRef> sorted;
  sorted.reserve(entries.size());
  for (auto& entry : entries) {
    sorted.push_back(std::move(entry.member));
  }
  return sorted;
}

static void CollectTypePaths(const Type& type, std::vector<TypePath>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) {
          return;
        } else if constexpr (std::is_same_v<T, TypeVar>) {
          return;
        } else if constexpr (std::is_same_v<T, TypeRange>) {
          CollectTypePaths(*node.base, out);
          return;
        } else if constexpr (std::is_same_v<T, TypeRangeInclusive>) {
          CollectTypePaths(*node.base, out);
          return;
        } else if constexpr (std::is_same_v<T, TypeRangeFrom>) {
          CollectTypePaths(*node.base, out);
          return;
        } else if constexpr (std::is_same_v<T, TypeRangeTo>) {
          CollectTypePaths(*node.base, out);
          return;
        } else if constexpr (std::is_same_v<T, TypeRangeToInclusive>) {
          CollectTypePaths(*node.base, out);
          return;
        } else if constexpr (std::is_same_v<T, TypeRangeFull>) {
          return;
        } else if constexpr (std::is_same_v<T, TypePerm>) {
          CollectTypePaths(*node.base, out);
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            CollectTypePaths(*elem, out);
          }
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          CollectTypePaths(*node.element, out);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          CollectTypePaths(*node.element, out);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (const auto& member : node.members) {
            CollectTypePaths(*member, out);
          }
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            CollectTypePaths(*param.type, out);
          }
          CollectTypePaths(*node.ret, out);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          CollectTypePaths(*node.element, out);
        } else if constexpr (std::is_same_v<T, TypeRawPtr>) {
          CollectTypePaths(*node.element, out);
        } else if constexpr (std::is_same_v<T, TypeString>) {
          return;
        } else if constexpr (std::is_same_v<T, TypeBytes>) {
          return;
        } else if constexpr (std::is_same_v<T, TypeDynamic>) {
          out.push_back(node.path);
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          out.push_back(ModalRefPath(node.modal_ref));
          for (const auto& arg : ModalRefArgs(node.modal_ref)) {
            CollectTypePaths(*arg, out);
          }
        } else if constexpr (std::is_same_v<T, TypeOpaque>) {
          out.push_back(node.class_path);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          CollectTypePaths(*node.base, out);
        } else if constexpr (std::is_same_v<T, TypePathType>) {
          out.push_back(node.path);
          for (const auto& arg : node.generic_args) {
            CollectTypePaths(*arg, out);
          }
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          out.push_back(node.path);
          for (const auto& arg : node.args) {
            CollectTypePaths(*arg, out);
          }
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& [is_move, param_type] : node.params) {
            if (param_type) {
              CollectTypePaths(*param_type, out);
            }
          }
          if (node.ret) {
            CollectTypePaths(*node.ret, out);
          }
        }
      },
      type.node);
}

std::vector<TypePath> TypePaths(const Type& type) {
  SpecDefsTypePaths();
  std::vector<TypePath> out;
  CollectTypePaths(type, out);
  std::stable_sort(out.begin(), out.end(), PathLess);
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<TypePath> TypePaths(const TypeRef& type) {
  SpecDefsTypePaths();
  if (!type) {
    return {};
  }
  return TypePaths(*type);
}

bool IsRangeType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm = std::get_if<TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&stripped->node)) {
      stripped = refine->base;
      continue;
    }
    break;
  }
  if (!stripped) {
    return false;
  }
  return std::holds_alternative<TypeRange>(stripped->node) ||
         std::holds_alternative<TypeRangeInclusive>(stripped->node) ||
         std::holds_alternative<TypeRangeFrom>(stripped->node) ||
         std::holds_alternative<TypeRangeTo>(stripped->node) ||
         std::holds_alternative<TypeRangeToInclusive>(stripped->node) ||
         std::holds_alternative<TypeRangeFull>(stripped->node);
}

std::optional<TypeRef> RangeElementType(const TypeRef& type) {
  if (!type) {
    return std::nullopt;
  }
  TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm = std::get_if<TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&stripped->node)) {
      stripped = refine->base;
      continue;
    }
    break;
  }
  if (!stripped) {
    return std::nullopt;
  }
  if (const auto* range = std::get_if<TypeRange>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeInclusive>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeFrom>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeTo>(&stripped->node)) {
    return range->base;
  }
  if (const auto* range = std::get_if<TypeRangeToInclusive>(&stripped->node)) {
    return range->base;
  }
  return std::nullopt;
}

bool IsRangeIndexType(const TypeRef& type) {
  if (!type) {
    return false;
  }
  TypeRef stripped = type;
  while (stripped) {
    if (const auto* perm = std::get_if<TypePerm>(&stripped->node)) {
      stripped = perm->base;
      continue;
    }
    if (const auto* refine = std::get_if<TypeRefine>(&stripped->node)) {
      stripped = refine->base;
      continue;
    }
    break;
  }
  if (!stripped) {
    return false;
  }

  auto is_usize = [](const TypeRef& t) {
    const auto* prim = t ? std::get_if<TypePrim>(&t->node) : nullptr;
    return prim && IdEq(prim->name, "usize");
  };

  if (std::holds_alternative<TypeRangeFull>(stripped->node)) {
    return true;
  }
  const auto elem = RangeElementType(stripped);
  return elem.has_value() && is_usize(*elem);
}

// C0X Extension: Async type helpers (§19.1)

bool IdEq(const std::string& a, const std::string& b) {
  return a == b;
}

// Check if path matches the Async built-in modal type
bool IsAsyncModalPath(const TypePath& path) {
  return PathMatchesBuiltinName(path, "Async");
}

// Check if path matches a built-in async type alias
bool IsAsyncAliasPath(const TypePath& path) {
  return PathMatchesBuiltinName(path, "Future") ||
         PathMatchesBuiltinName(path, "Sequence") ||
         PathMatchesBuiltinName(path, "Stream") ||
         PathMatchesBuiltinName(path, "Pipe") ||
         PathMatchesBuiltinName(path, "Exchange");
}

namespace {

std::optional<AsyncSig> AsyncSigFromBuiltinName(
    const std::string& name,
    const std::vector<TypeRef>& generic_args) {
  auto unit_type = MakeTypePrim("()");
  auto never_type = MakeTypePrim("!");

  if (IdEq(name, "Async")) {
    AsyncSig sig;
    sig.out = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.in = generic_args.size() > 1 ? generic_args[1] : unit_type;
    sig.result = generic_args.size() > 2 ? generic_args[2] : unit_type;
    sig.err = generic_args.size() > 3 ? generic_args[3] : never_type;
    return sig;
  }
  if (IdEq(name, "Future")) {
    AsyncSig sig;
    sig.out = unit_type;
    sig.in = unit_type;
    sig.result = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.err = generic_args.size() > 1 ? generic_args[1] : never_type;
    return sig;
  }
  if (IdEq(name, "Sequence")) {
    AsyncSig sig;
    sig.out = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.in = unit_type;
    sig.result = unit_type;
    sig.err = never_type;
    return sig;
  }
  if (IdEq(name, "Stream")) {
    AsyncSig sig;
    sig.out = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.in = unit_type;
    sig.result = unit_type;
    sig.err = generic_args.size() > 1 ? generic_args[1] : never_type;
    return sig;
  }
  if (IdEq(name, "Pipe")) {
    AsyncSig sig;
    sig.out = generic_args.size() > 1 ? generic_args[1] : unit_type;
    sig.in = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.result = unit_type;
    sig.err = never_type;
    return sig;
  }
  if (IdEq(name, "Exchange")) {
    AsyncSig sig;
    auto t = generic_args.size() > 0 ? generic_args[0] : unit_type;
    sig.out = t;
    sig.in = t;
    sig.result = t;
    sig.err = never_type;
    return sig;
  }
  return std::nullopt;
}

}  // namespace

bool IsAsyncType(const TypeRef& type) {
  SPEC_RULE("AsyncSig");

  return GetAsyncSig(type).has_value();
}

std::optional<AsyncSig> GetAsyncSig(const TypeRef& type) {
  SPEC_RULE("AsyncSig-Extract");

  if (!type) return std::nullopt;

  return std::visit(
      [&](const auto& node) -> std::optional<AsyncSig> {
        using T = std::decay_t<decltype(node)>;

        if constexpr (std::is_same_v<T, TypePathType>) {
          // Async built-ins are recognized by canonical builtin names.
          if (IsAsyncModalPath(node.path) || IsAsyncAliasPath(node.path)) {
            return AsyncSigFromBuiltinName(node.path.back(), node.generic_args);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeApply>) {
          if (IsAsyncModalPath(node.path) || IsAsyncAliasPath(node.path)) {
            return AsyncSigFromBuiltinName(node.path.back(), node.args);
          }
          return std::nullopt;
        } else if constexpr (std::is_same_v<T, TypeModalState>) {
          // Async@Suspended, Async@Completed, Async@Failed
          if (IsAsyncModalPath(ModalRefPath(node.modal_ref))) {
            return AsyncSigFromBuiltinName("Async", ModalRefArgs(node.modal_ref));
          }
          return std::nullopt;
        } else {
          return std::nullopt;
        }
      },
      type->node);
}

std::optional<AsyncSig> AsyncSigOf(const ScopeContext& ctx, const TypeRef& type) {
  if (const auto sig = GetAsyncSig(type)) {
    return sig;
  }

  if (!type) {
    return std::nullopt;
  }

  const TypePath* path_name = AppliedTypePath(*type);
  const std::vector<TypeRef>* path_args = AppliedTypeArgs(*type);

  if (!path_name || path_name->empty()) {
    return std::nullopt;
  }

  const auto* alias = LookupTypeAliasDecl(ctx, *path_name);
  if (!alias) {
    return std::nullopt;
  }

  const auto lowered = LowerType(ctx, alias->type);
  if (!lowered.ok) {
    return std::nullopt;
  }

  TypeRef instantiated = lowered.type;
  if (alias->generic_params.has_value()) {
    const auto& params = alias->generic_params->params;
    if (path_args->size() > params.size()) {
      return std::nullopt;
    }
    const auto subst = BuildSubstitution(params, *path_args);
    instantiated = InstantiateType(instantiated, subst);
    if (!instantiated) {
      return std::nullopt;
    }
  } else if (!path_args->empty()) {
    return std::nullopt;
  }

  return AsyncSigOf(ctx, instantiated);
}

}  // namespace cursive::analysis
