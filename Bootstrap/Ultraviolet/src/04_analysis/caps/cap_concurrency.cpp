// UVX Extension: Structured Concurrency Built-in Types (§18)

#include "04_analysis/caps/cap_concurrency.h"

#include <array>
#include <utility>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsConcurrency() {
  SPEC_DEF("ExecutionDomainClass", "18.2.4");
  SPEC_DEF("SpawnedModal", "18.4.2");
  SPEC_DEF("CancelTokenModal", "18.6.1");
  SPEC_DEF("TrackedModal", "5.4.4");
  SPEC_DEF("AsyncModal", "5.4.5");
  SPEC_DEF("ReactorClass", "5.9.2");
}

static std::shared_ptr<ast::Type> MakeTypeNode(const ast::TypeNode& node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = node;
  return ty;
}

static std::shared_ptr<ast::Type> MakeTypePrimAst(std::string_view name) {
  return MakeTypeNode(ast::TypePrim{ast::Identifier{name}});
}

static std::shared_ptr<ast::Type> MakeTypePathAst(
    std::initializer_list<std::string_view> comps) {
  ast::TypePath path;
  for (const auto comp : comps) {
    path.emplace_back(comp);
  }
  return MakeTypeNode(ast::TypePathType{std::move(path), {}});
}

static std::shared_ptr<ast::Type> MakeTypeApplyAst(
    std::string_view name,
    std::vector<std::shared_ptr<ast::Type>> args) {
  ast::TypePath path;
  path.emplace_back(name);
  ast::TypePathType path_type{std::move(path), std::move(args)};
  return MakeTypeNode(path_type);
}

static std::shared_ptr<ast::Type> MakeTypeUnionAst(
    std::vector<std::shared_ptr<ast::Type>> members) {
  ast::TypeUnion node;
  node.types = std::move(members);
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypePermAst(
    ast::TypePerm perm,
    std::shared_ptr<ast::Type> base) {
  ast::TypePermType node;
  node.perm = perm;
  node.base = std::move(base);
  return MakeTypeNode(std::move(node));
}

static ast::Param MakeParam(std::string_view name,
                            std::shared_ptr<ast::Type> type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = std::string(name);
  param.type = std::move(type);
  return param;
}

static std::shared_ptr<ast::Type> MakeTypeModalStateAst(
    std::initializer_list<std::string_view> comps,
    std::string_view state) {
  ast::TypeModalState modal;
  for (const auto comp : comps) {
    modal.path.emplace_back(comp);
  }
  ast::SyncTypeModalStateFromFields(modal);
  modal.state = ast::Identifier{state};
  return MakeTypeNode(modal);
}

static std::shared_ptr<ast::Block> MakeEmptyBlock() {
  auto block = std::make_shared<ast::Block>();
  block->stmts = {};
  block->tail_opt = nullptr;
  return block;
}

static ast::TypeParam MakeTypeParam(std::string_view name,
                                    std::shared_ptr<ast::Type> default_type) {
  ast::TypeParam param{};
  param.name = std::string(name);
  param.default_type = std::move(default_type);
  param.span = core::Span{};
  return param;
}

static std::optional<ast::GenericParams> MakeGenericParams(
    std::vector<ast::TypeParam> params) {
  ast::GenericParams gen{};
  gen.params = std::move(params);
  gen.span = core::Span{};
  return gen;
}

static ast::ClassMethodDecl MakeClassMethod(
    std::string_view name,
    std::optional<ast::GenericParams> generic_params,
    ast::Receiver receiver,
    std::vector<ast::Param> params,
    std::shared_ptr<ast::Type> ret) {
  ast::ClassMethodDecl method{};
  method.vis = ast::Visibility::Public;
  method.name = std::string(name);
  method.generic_params = std::move(generic_params);
  method.receiver = std::move(receiver);
  method.params = std::move(params);
  method.return_type_opt = std::move(ret);
  method.body_opt = nullptr;
  method.span = core::Span{};
  return method;
}

static ast::StateFieldDecl MakeStateField(std::string_view name,
                                          std::shared_ptr<ast::Type> type) {
  ast::StateFieldDecl field{};
  field.vis = ast::Visibility::Public;
  field.name = std::string(name);
  field.type = std::move(type);
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

static ast::StateMethodDecl MakeStateMethod(
    std::string_view name,
    std::shared_ptr<ast::Type> ret,
    ast::Receiver receiver =
        ast::ReceiverShorthand{ast::ReceiverPerm::Const}) {
  ast::StateMethodDecl method;
  method.vis = ast::Visibility::Public;
  method.name = std::string(name);
  method.receiver = std::move(receiver);
  method.params = {};
  method.return_type_opt = std::move(ret);
  method.body = MakeEmptyBlock();
  method.span = core::Span{};
  method.doc_opt = std::nullopt;
  return method;
}

static TypeRef TypeUnit() {
  return MakeTypePrim("()");
}

static TypeRef TypeBool() {
  return MakeTypePrim("bool");
}

static TypeRef TypeUsize() {
  return MakeTypePrim("usize");
}

static constexpr std::array<std::string_view, 6> kGpuTripletIntrinsics = {
    "gpu_global_id",
    "gpu_local_id",
    "gpu_workgroup_id",
    "gpu_workgroup_size",
    "gpu_global_size",
    "gpu_num_workgroups",
};

static constexpr std::array<std::string_view, 1> kGpuScalarIntrinsics = {
    "gpu_linear_id",
};

static constexpr std::array<std::string_view, 3> kGpuBarrierIntrinsics = {
    "gpu_barrier",
    "gpu_memory_barrier",
    "gpu_workgroup_barrier",
};

template <std::size_t N>
static bool IsNameIn(std::string_view name,
                     const std::array<std::string_view, N>& names) {
  for (const auto candidate : names) {
    if (IdEq(name, candidate)) {
      return true;
    }
  }
  return false;
}

static TypeRef TypeUsize3Tuple() {
  std::vector<TypeRef> members;
  members.reserve(3);
  members.push_back(TypeUsize());
  members.push_back(TypeUsize());
  members.push_back(TypeUsize());
  return MakeTypeTuple(std::move(members));
}

// Helper for string comparison
static bool StrEq(std::string_view a, std::string_view b) {
  return a == b;
}

}  // namespace

// -----------------------------------------------------------------------------
// ExecutionDomain class (§18.2.4)
// -----------------------------------------------------------------------------

bool IsExecutionDomainClassPath(const ast::ClassPath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "ExecutionDomain");
}

bool IsExecutionDomainTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return IsExecutionDomainClassPath(path) || IsCpuDomainTypePath(path) ||
         IsGpuDomainTypePath(path) || IsInlineDomainTypePath(path);
}

bool IsCpuDomainTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "CpuDomain");
}

bool IsGpuDomainTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "GpuDomain");
}

bool IsInlineDomainTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "InlineDomain");
}

bool IsGpuIntrinsicName(std::string_view name) {
  return IsNameIn(name, kGpuTripletIntrinsics) ||
         IsNameIn(name, kGpuScalarIntrinsics) ||
         IsNameIn(name, kGpuBarrierIntrinsics);
}

bool IsGpuExecutionBarrierName(std::string_view name) {
  static constexpr std::array<std::string_view, 2> kGpuExecutionBarriers = {
      "gpu_barrier",
      "gpu_workgroup_barrier",
  };
  return IsNameIn(name, kGpuExecutionBarriers);
}

std::optional<TypeRef> LookupGpuIntrinsicType(std::string_view name) {
  SpecDefsConcurrency();
  if (IsNameIn(name, kGpuTripletIntrinsics)) {
    return MakeTypeFunc({}, TypeUsize3Tuple());
  }
  if (IsNameIn(name, kGpuScalarIntrinsics)) {
    return MakeTypeFunc({}, TypeUsize());
  }
  if (IsNameIn(name, kGpuBarrierIntrinsics)) {
    return MakeTypeFunc({}, TypeUnit());
  }
  return std::nullopt;
}

std::optional<ExecutionDomainMethodSig> LookupExecutionDomainMethodSig(
    std::string_view name) {
  SpecDefsConcurrency();
  ExecutionDomainMethodSig sig{};

  // §18.2.4: procedure name(~) -> string
  if (StrEq(name, "name")) {
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = MakeTypeString(StringState::View);
    return sig;
  }

  // §18.2.4: procedure max_concurrency(~) -> usize
  if (StrEq(name, "max_concurrency")) {
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = TypeUsize();
    return sig;
  }

  return std::nullopt;
}

static ast::ClassDecl BuildDomainClassDecl(std::string_view name) {
  ast::ClassDecl decl;
  decl.vis = ast::Visibility::Public;
  decl.name = std::string(name);
  decl.supers = {{"ExecutionDomain"}};
  decl.items = {};
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ClassDecl BuildCpuDomainClassDecl() {
  SpecDefsConcurrency();
  return BuildDomainClassDecl("CpuDomain");
}

ast::ClassDecl BuildGpuDomainClassDecl() {
  SpecDefsConcurrency();
  return BuildDomainClassDecl("GpuDomain");
}

ast::ClassDecl BuildInlineDomainClassDecl() {
  SpecDefsConcurrency();
  return BuildDomainClassDecl("InlineDomain");
}

// -----------------------------------------------------------------------------
// Spawned<T> modal type (§18.4.2)
// -----------------------------------------------------------------------------

bool IsSpawnedTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "Spawned");
}

bool IsValidSpawnedState(std::string_view state) {
  SpecDefsConcurrency();
  return StrEq(state, "Pending") || StrEq(state, "Ready");
}

TypeRef MakeSpawnedType(const TypeRef& inner_type) {
  SpecDefsConcurrency();
  TypePathType path_type;
  path_type.path = {"Spawned"};
  path_type.generic_args = {inner_type};
  return MakeType(path_type);
}

TypeRef MakeSpawnedTypeWithState(const TypeRef& inner_type,
                                 std::string_view state) {
  SpecDefsConcurrency();
  return MakeTypeModalState({"Spawned"}, std::string(state), {inner_type});
}

std::optional<TypeRef> ExtractSpawnedInner(const TypeRef& type) {
  SpecDefsConcurrency();
  if (!type) return std::nullopt;

  // Check for TypePathType with Spawned path
  const auto* path = std::get_if<TypePathType>(&type->node);
  if (path && IsSpawnedTypePath(path->path)) {
    if (!path->generic_args.empty()) {
      return path->generic_args[0];
    }
    return std::nullopt;
  }

  // Check for TypeModalState with Spawned path
  const auto* modal = std::get_if<TypeModalState>(&type->node);
  if (modal && IsSpawnedTypePath(modal->path)) {
    if (!modal->generic_args.empty()) {
      return modal->generic_args[0];
    }
    return std::nullopt;
  }

  return std::nullopt;
}

// -----------------------------------------------------------------------------
// Tracked<T, E> modal type (§5.4.4)
// -----------------------------------------------------------------------------

bool IsTrackedTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "Tracked");
}

bool IsValidTrackedState(std::string_view state) {
  SpecDefsConcurrency();
  return StrEq(state, "Pending") || StrEq(state, "Ready");
}

TypeRef MakeTrackedType(const TypeRef& value_type,
                        const TypeRef& err_type) {
  SpecDefsConcurrency();
  TypePathType path_type;
  path_type.path = {"Tracked"};
  path_type.generic_args = {value_type, err_type};
  return MakeType(path_type);
}

TypeRef MakeTrackedTypeWithState(const TypeRef& value_type,
                                 const TypeRef& err_type,
                                 std::string_view state) {
  SpecDefsConcurrency();
  return MakeTypeModalState({"Tracked"}, std::string(state),
                            {value_type, err_type});
}

std::optional<std::pair<TypeRef, TypeRef>> ExtractTrackedArgs(
    const TypeRef& type) {
  SpecDefsConcurrency();
  if (!type) {
    return std::nullopt;
  }
  const auto* path = std::get_if<TypePathType>(&type->node);
  if (path && IsTrackedTypePath(path->path)) {
    if (path->generic_args.size() == 2) {
      return std::make_pair(path->generic_args[0], path->generic_args[1]);
    }
    return std::nullopt;
  }
  const auto* modal = std::get_if<TypeModalState>(&type->node);
  if (modal && IsTrackedTypePath(modal->path)) {
    if (modal->generic_args.size() == 2) {
      return std::make_pair(modal->generic_args[0], modal->generic_args[1]);
    }
    return std::nullopt;
  }
  return std::nullopt;
}

// -----------------------------------------------------------------------------
// CancelToken modal type (§18.6.1)
// -----------------------------------------------------------------------------

bool IsCancelTokenTypePath(const ast::TypePath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "CancelToken");
}

bool IsValidCancelTokenState(std::string_view state) {
  SpecDefsConcurrency();
  return StrEq(state, "Active");
}

std::optional<CancelTokenMethodSig> LookupCancelTokenMethodSig(
    std::string_view name,
    std::string_view state) {
  SpecDefsConcurrency();
  CancelTokenMethodSig sig{};

  // §20.6.4: CancelToken@Active::cancel() - only in @Active state
  if (StrEq(name, "cancel")) {
    if (!StrEq(state, "Active") && !state.empty()) {
      return std::nullopt;  // Only valid in @Active state
    }
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = TypeUnit();
    sig.valid_states = "Active";
    return sig;
  }

  // §18.6.1: procedure is_cancelled(~) -> bool - only in @Active state
  if (StrEq(name, "is_cancelled")) {
    if (!StrEq(state, "Active") && !state.empty()) {
      return std::nullopt;
    }
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = TypeBool();
    sig.valid_states = "Active";
    return sig;
  }

  if (StrEq(name, "wait_cancelled")) {
    if (!StrEq(state, "Active") && !state.empty()) {
      return std::nullopt;
    }
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = MakeTypePath({"Async"}, {TypeUnit()});
    sig.valid_states = "Active";
    return sig;
  }

  // §18.6.1: procedure child(~) -> CancelToken@Active - only in @Active state
  if (StrEq(name, "child")) {
    if (!StrEq(state, "Active") && !state.empty()) {
      return std::nullopt;  // Only valid in @Active state
    }
    sig.recv_perm = Permission::Const;
    sig.params = {};
    sig.ret = MakeCancelTokenTypeWithState("Active");
    sig.valid_states = "Active";
    return sig;
  }

  // §18.6.1: procedure new() -> CancelToken@Active - static constructor
  if (StrEq(name, "new")) {
    sig.recv_perm = Permission::Const;  // Not used for static
    sig.params = {};
    sig.ret = MakeCancelTokenTypeWithState("Active");
    sig.valid_states = "";  // Static method
    return sig;
  }

  return std::nullopt;
}

TypeRef MakeCancelTokenType() {
  SpecDefsConcurrency();
  TypePathType path_type;
  path_type.path = {"CancelToken"};
  path_type.generic_args = {};
  return MakeType(path_type);
}

TypeRef MakeCancelTokenTypeWithState(std::string_view state) {
  SpecDefsConcurrency();
  return MakeTypeModalState({"CancelToken"}, std::string(state));
}

// -----------------------------------------------------------------------------
// Built-in type declarations for sigma population
// -----------------------------------------------------------------------------

ast::ModalDecl BuildSpawnedModalDecl() {
  SpecDefsConcurrency();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Spawned";
  decl.generic_params = MakeGenericParams({MakeTypeParam("T", nullptr)});
  decl.implements = {};

  // State @Pending - task has been created but not completed
  {
    ast::StateBlock pending_state{};
    pending_state.name = "Pending";
    pending_state.members = {};
    decl.states.push_back(pending_state);
  }

  // State @Ready - task has completed, value is available
  {
    ast::StateBlock ready_state{};
    ready_state.name = "Ready";
    ready_state.members = {
        MakeStateField("value", MakeTypePathAst({"T"})),
    };
    decl.states.push_back(ready_state);
  }

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ModalDecl BuildCancelTokenModalDecl() {
  SpecDefsConcurrency();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "CancelToken";
  decl.implements = {};
  auto id_field = MakeStateField("id", MakeTypePrimAst("usize"));
  id_field.vis = ast::Visibility::Private;
  auto cancel_method = MakeStateMethod(
      "cancel", MakeTypePrimAst("()"),
      ast::ReceiverShorthand{ast::ReceiverPerm::Const});

  // State @Active
  {
    ast::StateBlock active_state{};
    active_state.name = "Active";
    active_state.members = {
        id_field,
        cancel_method,
        MakeStateMethod("is_cancelled", MakeTypePrimAst("bool")),
        MakeStateMethod("wait_cancelled",
                        MakeTypeApplyAst("Async", {MakeTypePrimAst("()")})),
        MakeStateMethod("child", MakeTypeModalStateAst({"CancelToken"}, "Active")),
    };
    decl.states.push_back(active_state);
  }

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ModalDecl BuildTrackedModalDecl() {
  SpecDefsConcurrency();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Tracked";
  decl.generic_params =
      MakeGenericParams({MakeTypeParam("T", nullptr), MakeTypeParam("E", nullptr)});
  decl.implements = {};

  {
    ast::StateBlock pending_state{};
    pending_state.name = "Pending";
    pending_state.members = {};
    decl.states.push_back(pending_state);
  }
  {
    ast::StateBlock ready_state{};
    ready_state.name = "Ready";
    std::vector<std::shared_ptr<ast::Type>> union_members;
    union_members.push_back(MakeTypePathAst({"T"}));
    union_members.push_back(MakeTypePathAst({"E"}));
    ready_state.members = {
        MakeStateField("value", MakeTypeUnionAst(std::move(union_members))),
    };
    decl.states.push_back(ready_state);
  }

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ModalDecl BuildAsyncModalDecl() {
  SpecDefsConcurrency();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Async";
  decl.generic_params = MakeGenericParams({
      MakeTypeParam("TOut", nullptr),
      MakeTypeParam("TIn", MakeTypePrimAst("()")),
      MakeTypeParam("TResult", MakeTypePrimAst("()")),
      MakeTypeParam("TError", MakeTypePrimAst("!")),
  });
  decl.implements = {};

  // Build Async<TOut, TIn, TResult, TError>@State modal state refs.
  auto async_state = [&](std::string_view state) {
    ast::TypeModalState modal;
    modal.path = {"Async"};
    modal.state = ast::Identifier{state};
    modal.generic_args = {
        MakeTypePathAst({"TOut"}),
        MakeTypePathAst({"TIn"}),
        MakeTypePathAst({"TResult"}),
        MakeTypePathAst({"TError"}),
    };
    ast::SyncTypeModalStateFromFields(modal);
    return MakeTypeNode(modal);
  };
  auto unique_async_state = [&](std::string_view state) {
    return MakeTypePermAst(ast::TypePerm::Unique, async_state(state));
  };

  // @Suspended state
  {
    ast::StateBlock suspended{};
    suspended.name = "Suspended";
    std::vector<std::shared_ptr<ast::Type>> resume_union;
    resume_union.push_back(unique_async_state("Suspended"));
    resume_union.push_back(unique_async_state("Completed"));
    resume_union.push_back(unique_async_state("Failed"));
    ast::StateMethodDecl resume;
    resume.vis = ast::Visibility::Public;
    resume.name = "resume";
    resume.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
    resume.params = {MakeParam("input", MakeTypePathAst({"TIn"}))};
    resume.return_type_opt = MakeTypeUnionAst(std::move(resume_union));
    resume.body = MakeEmptyBlock();
    resume.span = core::Span{};
    resume.doc_opt = std::nullopt;
    suspended.members = {
        MakeStateField("output", MakeTypePathAst({"TOut"})),
        resume,
    };
    decl.states.push_back(suspended);
  }

  // @Completed state
  {
    ast::StateBlock completed{};
    completed.name = "Completed";
    completed.members = {
        MakeStateField("value", MakeTypePathAst({"TResult"})),
    };
    decl.states.push_back(completed);
  }

  // @Failed state
  {
    ast::StateBlock failed{};
    failed.name = "Failed";
    failed.members = {
        MakeStateField("error", MakeTypePathAst({"TError"})),
    };
    decl.states.push_back(failed);
  }

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ClassDecl BuildAsyncClassDecl() {
  SpecDefsConcurrency();
  ast::ClassDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Async";
  decl.modal = false;
  decl.generic_params = MakeGenericParams({
      MakeTypeParam("TOut", nullptr),
      MakeTypeParam("TIn", nullptr),
      MakeTypeParam("TResult", nullptr),
      MakeTypeParam("TError", nullptr),
  });
  decl.supers = {};
  decl.predicate_clause_opt = std::nullopt;
  decl.items = {};
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

// -----------------------------------------------------------------------------
// Built-in async aliases (§5.4.5)
// -----------------------------------------------------------------------------

ast::TypeAliasDecl BuildSequenceAliasDecl() {
  SpecDefsConcurrency();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Sequence";
  decl.generic_params = MakeGenericParams({MakeTypeParam("T", nullptr)});
  ast::TypePathType body{};
  body.path = {"Async"};
  body.generic_args = {MakeTypePathAst({"T"}),
                       MakeTypePrimAst("()"),
                       MakeTypePrimAst("()"),
                       MakeTypePrimAst("!")};
  decl.type = MakeTypeNode(body);
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::TypeAliasDecl BuildFutureAliasDecl() {
  SpecDefsConcurrency();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Future";
  decl.generic_params =
      MakeGenericParams({MakeTypeParam("TResult", nullptr),
                         MakeTypeParam("TError", MakeTypePrimAst("!"))});
  ast::TypePathType body{};
  body.path = {"Async"};
  body.generic_args = {MakeTypePrimAst("()"),
                       MakeTypePrimAst("()"),
                       MakeTypePathAst({"TResult"}),
                       MakeTypePathAst({"TError"})};
  decl.type = MakeTypeNode(body);
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::TypeAliasDecl BuildStreamAliasDecl() {
  SpecDefsConcurrency();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Stream";
  decl.generic_params =
      MakeGenericParams({MakeTypeParam("TValue", nullptr),
                         MakeTypeParam("TError", nullptr)});
  ast::TypePathType body{};
  body.path = {"Async"};
  body.generic_args = {MakeTypePathAst({"TValue"}),
                       MakeTypePrimAst("()"),
                       MakeTypePrimAst("()"),
                       MakeTypePathAst({"TError"})};
  decl.type = MakeTypeNode(body);
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::TypeAliasDecl BuildPipeAliasDecl() {
  SpecDefsConcurrency();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Pipe";
  decl.generic_params =
      MakeGenericParams({MakeTypeParam("TIn", nullptr),
                         MakeTypeParam("TOut", nullptr)});
  ast::TypePathType body{};
  body.path = {"Async"};
  body.generic_args = {MakeTypePathAst({"TOut"}),
                       MakeTypePathAst({"TIn"}),
                       MakeTypePrimAst("()"),
                       MakeTypePrimAst("!")};
  decl.type = MakeTypeNode(body);
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::TypeAliasDecl BuildExchangeAliasDecl() {
  SpecDefsConcurrency();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Exchange";
  decl.generic_params = MakeGenericParams({MakeTypeParam("TValue", nullptr)});
  ast::TypePathType body{};
  body.path = {"Async"};
  body.generic_args = {MakeTypePathAst({"TValue"}),
                       MakeTypePathAst({"TValue"}),
                       MakeTypePathAst({"TValue"}),
                       MakeTypePrimAst("!")};
  decl.type = MakeTypeNode(body);
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::ClassDecl BuildExecutionDomainClassDecl() {
  SpecDefsConcurrency();
  ast::ClassDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "ExecutionDomain";
  decl.modal = false;
  decl.generic_params = std::nullopt;
  decl.supers = {};
  decl.predicate_clause_opt = std::nullopt;
  decl.items = {};
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

bool IsReactorClassPath(const ast::ClassPath& path) {
  SpecDefsConcurrency();
  return PathMatchesBuiltinName(path, "Reactor");
}

ast::ClassDecl BuildReactorClassDecl() {
  SpecDefsConcurrency();
  ast::ClassDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Reactor";
  decl.modal = false;
  decl.generic_params = std::nullopt;
  decl.supers = {};
  decl.predicate_clause_opt = std::nullopt;
  auto make_reactor_generics = []() {
    return MakeGenericParams({
        MakeTypeParam("T", nullptr),
        MakeTypeParam("E", nullptr),
    });
  };

  const auto type_t = MakeTypePathAst({"T"});
  const auto type_e = MakeTypePathAst({"E"});
  const auto future_ty = MakeTypeApplyAst("Future", {type_t, type_e});
  const auto tracked_ty = MakeTypeApplyAst("Tracked", {type_t, type_e});
  const auto result_union = MakeTypeUnionAst({type_t, type_e});

  ast::ClassMethodDecl run_method = MakeClassMethod(
      "run",
      make_reactor_generics(),
      ast::ReceiverShorthand{ast::ReceiverPerm::Const},
      {MakeParam("future", future_ty)},
      result_union);

  ast::ClassMethodDecl register_method = MakeClassMethod(
      "register",
      make_reactor_generics(),
      ast::ReceiverShorthand{ast::ReceiverPerm::Const},
      {MakeParam("future", future_ty)},
      tracked_ty);

  decl.items = {run_method, register_method};
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

}  // namespace ultraviolet::analysis
