#include "04_analysis/modal/builtin_modal_intrinsics.h"

#include "00_core/symbols.h"
#include "01_project/language_profile.h"
#include "04_analysis/caps/builtin_paths.h"

#include <array>

namespace cursive::analysis {

namespace {

bool IsSingleSegment(const TypePath& path, std::string_view name) {
  return path.size() == 1 && IdEq(path[0], std::string(name));
}

template <std::size_t N>
bool IsSingleSegmentOneOf(const TypePath& path,
                          const std::array<std::string_view, N>& names) {
  if (path.size() != 1) {
    return false;
  }
  for (const auto name : names) {
    if (IdEq(path[0], name)) {
      return true;
    }
  }
  return false;
}

constexpr std::array<std::string_view, 7> kBuiltinModalNames = {
    "Region",
    "File",
    "DirIter",
    "CancelToken",
    "Spawned",
    "Tracked",
    "Async",
};

constexpr std::array<std::string_view, 6> kBuiltinModalNoRecordLiteralNames = {
    "File",
    "DirIter",
    "CancelToken",
    "Spawned",
    "Tracked",
    "Async",
};

struct AsyncCombinatorEntry {
  std::string_view name;
  BuiltinAsyncCombinatorKind kind = BuiltinAsyncCombinatorKind::Map;
};

constexpr std::array<AsyncCombinatorEntry, 5> kAsyncCombinators = {{
    {"map", BuiltinAsyncCombinatorKind::Map},
    {"filter", BuiltinAsyncCombinatorKind::Filter},
    {"take", BuiltinAsyncCombinatorKind::Take},
    {"fold", BuiltinAsyncCombinatorKind::Fold},
    {"chain", BuiltinAsyncCombinatorKind::Chain},
}};

TypeRef TypeUnit() {
  return MakeTypePrim("()");
}

TypeRef TypeRawMutU8() {
  return MakeTypeRawPtr(RawPtrQual::Mut, MakeTypePrim("u8"));
}

TypeRef TypeRegionState(std::string_view state) {
  return MakeTypeModalState({"Region"}, std::string(state));
}

TypeRef TypeCancelTokenState(std::string_view state) {
  return MakeTypeModalState({"CancelToken"}, std::string(state));
}

std::optional<BuiltinAsyncCombinatorKind> LookupAsyncCombinatorKindImpl(
    std::string_view member_name) {
  for (const auto& entry : kAsyncCombinators) {
    if (IdEq(member_name, entry.name)) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

bool IsAsyncCombinatorName(std::string_view member_name) {
  return LookupAsyncCombinatorKindImpl(member_name).has_value();
}

std::optional<std::string> LookupAsyncRuntimeSymbolFor(
    std::optional<std::string_view> state,
    std::string_view member_name);

bool IsAsyncCombinatorRuntimeSymbol(std::string_view symbol,
                                    std::string_view member_name) {
  const auto runtime_symbol = LookupAsyncRuntimeSymbolFor(std::nullopt, member_name);
  return runtime_symbol.has_value() && symbol == *runtime_symbol;
}

BuiltinModalMemberSig RegionAllocSig() {
  BuiltinModalMemberSig sig;
  sig.recv_perm = Permission::Unique;
  sig.params.push_back(BuiltinModalParam{std::nullopt, nullptr});
  sig.ret = nullptr;
  sig.ret_from_first_arg = true;
  sig.lowering = BuiltinModalLoweringOp::AllocInReceiver;
  sig.allocates_in_receiver = true;
  sig.runtime_symbol = project::RuntimePathSig({"region", "alloc"});
  return sig;
}

std::optional<BuiltinModalMemberSig> LookupRegionMemberSig(
    std::string_view state,
    std::string_view member_name) {
  if (IdEq(member_name, "alloc") && IdEq(state, "Active")) {
    return RegionAllocSig();
  }

  if (IdEq(member_name, "reset_unchecked") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Unique;
    sig.ret = TypeRegionState("Active");
    sig.requires_unsafe = true;
    sig.unsafe_diag = "E-MEM-3030";
    sig.runtime_symbol =
        project::RuntimePathSig({"region", "reset_unchecked"});
    sig.consumes_receiver = true;
    return sig;
  }

  if (IdEq(member_name, "freeze") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Unique;
    sig.ret = TypeRegionState("Frozen");
    sig.runtime_symbol = project::RuntimePathSig({"region", "freeze"});
    sig.consumes_receiver = true;
    return sig;
  }

  if (IdEq(member_name, "thaw") && IdEq(state, "Frozen")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Unique;
    sig.ret = TypeRegionState("Active");
    sig.runtime_symbol = project::RuntimePathSig({"region", "thaw"});
    sig.consumes_receiver = true;
    return sig;
  }

  if (IdEq(member_name, "free_unchecked") &&
      (IdEq(state, "Active") || IdEq(state, "Frozen"))) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Unique;
    sig.ret = TypeRegionState("Freed");
    sig.requires_unsafe = true;
    sig.unsafe_diag = "E-MEM-3030";
    sig.runtime_symbol =
        project::RuntimePathSig({"region", "free_unchecked"});
    sig.consumes_receiver = true;
    return sig;
  }

  return std::nullopt;
}

std::optional<TypeFunc> LookupRegionStaticSig(std::string_view member_name) {
  if (!IdEq(member_name, "new_scoped")) {
    return std::nullopt;
  }
  std::vector<TypeFuncParam> params;
  params.push_back(TypeFuncParam{
      std::nullopt,
      MakeTypePath({"RegionOptions"})});
  return TypeFunc{
      std::move(params),
      MakeTypePerm(Permission::Unique, TypeRegionState("Active"))};
}

std::optional<BuiltinModalMemberSig> LookupCancelTokenMemberSig(
    std::string_view state,
    std::string_view member_name) {
  if (IdEq(member_name, "cancel") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Shared;
    sig.ret = TypeUnit();
    sig.runtime_symbol = core::PathSig({"CancelToken", "Active", "cancel"});
    return sig;
  }

  if (IdEq(member_name, "is_cancelled") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Const;
    sig.ret = MakeTypePrim("bool");
    sig.runtime_symbol =
        core::PathSig({"CancelToken", "Active", "is_cancelled"});
    return sig;
  }

  if (IdEq(member_name, "child") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Const;
    sig.ret = TypeCancelTokenState("Active");
    sig.runtime_symbol = core::PathSig({"CancelToken", "Active", "child"});
    return sig;
  }

  if (IdEq(member_name, "wait_cancelled") && IdEq(state, "Active")) {
    BuiltinModalMemberSig sig;
    sig.recv_perm = Permission::Const;
    sig.ret = MakeTypePath({"Async"}, {TypeUnit()});
    sig.runtime_symbol =
        core::PathSig({"CancelToken", "Active", "wait_cancelled"});
    return sig;
  }

  return std::nullopt;
}

std::optional<TypeFunc> LookupCancelTokenStaticSig(std::string_view member_name) {
  if (!IdEq(member_name, "new")) {
    return std::nullopt;
  }
  std::vector<TypeFuncParam> params;
  return TypeFunc{std::move(params), TypeCancelTokenState("Active")};
}

std::optional<std::string> LookupRegionRuntimeSymbolFor(
    std::optional<std::string_view> state,
    std::string_view member_name) {
  if (!state.has_value()) {
    if (IdEq(member_name, "new_scoped")) {
      return project::RuntimePathSig({"region", "new_scoped"});
    }
    return std::nullopt;
  }
  // Runtime-only Region helpers used by compiler-generated frame lowering.
  // These are not part of the source-level Region procedure surface (§5.4.1).
  if (IdEq(*state, "Active")) {
    if (IdEq(member_name, "mark")) {
      return project::RuntimePathSig({"region", "mark"});
    }
    if (IdEq(member_name, "reset_to")) {
      return project::RuntimePathSig({"region", "reset_to"});
    }
  }
  const auto sig = LookupRegionMemberSig(*state, member_name);
  if (!sig.has_value() || sig->runtime_symbol.empty()) {
    return std::nullopt;
  }
  return sig->runtime_symbol;
}

std::optional<std::string> LookupCancelRuntimeSymbolFor(
    std::optional<std::string_view> state,
    std::string_view member_name) {
  if (!state.has_value()) {
    if (IdEq(member_name, "new")) {
      return core::PathSig({"CancelToken", "new"});
    }
    return std::nullopt;
  }
  const auto sig = LookupCancelTokenMemberSig(*state, member_name);
  if (!sig.has_value() || sig->runtime_symbol.empty()) {
    return std::nullopt;
  }
  return sig->runtime_symbol;
}

std::optional<std::string> LookupAsyncRuntimeSymbolFor(
    std::optional<std::string_view> state,
    std::string_view member_name) {
  if (state.has_value()) {
    if (IdEq(*state, "Suspended") && IdEq(member_name, "resume")) {
      return project::RuntimePathSig({"async", "resume"});
    }
    return std::nullopt;
  }

  if (IsAsyncCombinatorName(member_name)) {
    return project::RuntimePathSig({"async", "combinator", member_name});
  }

  return std::nullopt;
}

}  // namespace

std::optional<BuiltinAsyncCombinatorKind> LookupBuiltinAsyncCombinator(
    std::string_view member_name) {
  return LookupAsyncCombinatorKindImpl(member_name);
}

std::optional<BuiltinAsyncCombinatorKind>
LookupBuiltinAsyncCombinatorByRuntimeSymbol(std::string_view symbol) {
  for (const auto& entry : kAsyncCombinators) {
    if (IsAsyncCombinatorRuntimeSymbol(symbol, entry.name)) {
      return entry.kind;
    }
  }
  return std::nullopt;
}

bool IsBuiltinModalTypePath(const TypePath& modal_path) {
  return IsSingleSegmentOneOf(modal_path, kBuiltinModalNames);
}

bool IsBuiltinModalRecordLiteralForbidden(const TypePath& modal_path) {
  return IsSingleSegmentOneOf(modal_path, kBuiltinModalNoRecordLiteralNames);
}

bool IsBuiltinRuntimeHandleModalTypePath(const TypePath& modal_path) {
  return PathMatchesBuiltinName(modal_path, "Spawned") ||
         PathMatchesBuiltinName(modal_path, "Tracked");
}

bool IsBuiltinModalGeneralMember(const TypePath& modal_path,
                                 std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Async")) {
    return IsAsyncCombinatorName(member_name);
  }
  return false;
}

bool IsBuiltinModalMemberName(const TypePath& modal_path,
                              std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Region")) {
    return LookupRegionStaticSig(member_name).has_value() ||
           LookupRegionMemberSig("Active", member_name).has_value() ||
           LookupRegionMemberSig("Frozen", member_name).has_value() ||
           LookupRegionMemberSig("Freed", member_name).has_value();
  }
  if (IsSingleSegment(modal_path, "CancelToken")) {
    return LookupCancelTokenStaticSig(member_name).has_value() ||
           LookupCancelTokenMemberSig("Active", member_name).has_value();
  }
  if (IsSingleSegment(modal_path, "Async")) {
    return IsBuiltinModalGeneralMember(modal_path, member_name) ||
           IdEq(member_name, "resume");
  }
  return false;
}

bool IsBuiltinModalStaticMemberName(const TypePath& modal_path,
                                    std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Region")) {
    return LookupRegionStaticSig(member_name).has_value();
  }
  if (IsSingleSegment(modal_path, "CancelToken")) {
    return LookupCancelTokenStaticSig(member_name).has_value();
  }
  return false;
}

std::optional<BuiltinModalMemberSig> LookupBuiltinModalMemberSig(
    const TypePath& modal_path,
    std::string_view state,
    std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Region")) {
    return LookupRegionMemberSig(state, member_name);
  }
  if (IsSingleSegment(modal_path, "CancelToken")) {
    return LookupCancelTokenMemberSig(state, member_name);
  }
  return std::nullopt;
}

std::optional<TypeFunc> LookupBuiltinModalStaticFuncSig(
    const TypePath& modal_path,
    std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Region")) {
    return LookupRegionStaticSig(member_name);
  }
  if (IsSingleSegment(modal_path, "CancelToken")) {
    return LookupCancelTokenStaticSig(member_name);
  }
  return std::nullopt;
}

std::optional<std::string> LookupBuiltinModalRuntimeSymbol(
    const TypePath& modal_path,
    std::optional<std::string_view> state,
    std::string_view member_name) {
  if (IsSingleSegment(modal_path, "Region")) {
    return LookupRegionRuntimeSymbolFor(state, member_name);
  }
  if (IsSingleSegment(modal_path, "CancelToken")) {
    return LookupCancelRuntimeSymbolFor(state, member_name);
  }
  if (IsSingleSegment(modal_path, "Async")) {
    return LookupAsyncRuntimeSymbolFor(state, member_name);
  }
  return std::nullopt;
}

std::optional<BuiltinModalLayoutInfo> LookupBuiltinModalLayout(
    const TypePath& modal_path) {
  if (!IsSingleSegment(modal_path, "Region")) {
    return std::nullopt;
  }
  BuiltinModalLayoutInfo info;
  info.size = 16;
  info.align = 8;
  info.disc_prim = "u8";
  info.payload_size = 8;
  info.payload_align = 8;
  return info;
}

}  // namespace cursive::analysis
