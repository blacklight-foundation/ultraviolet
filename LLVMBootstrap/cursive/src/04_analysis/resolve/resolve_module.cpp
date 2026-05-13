// =============================================================================
// resolve_module.cpp - Module Resolution Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   CursiveSpecification.md §5.1.7 "Resolution Pass" (Lines 7430-7549)
//   CursiveSpecification.md §5.1.5 "Top-Level Name Collection" (Lines 6956-7309)
//
// SOURCE FILE:
//   cursive-bootstrap/src/03_analysis/resolve/resolver_modules.cpp (Lines 1-498)
//
// MIGRATED: 2026-02-01
//
// =============================================================================

#include "04_analysis/resolve/resolver.h"

#include <utility>
#include <memory>
#include <string_view>
#include <string>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_intro.h"
#include "04_analysis/resolve/scopes_lookup.h"
#include "04_analysis/resolve/scope_overrides.h"
#include "04_analysis/resolve/visibility.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/caps/cap_comptime.h"
#include "04_analysis/caps/cap_system.h"
#include "04_analysis/caps/cap_filesystem.h"
#include "04_analysis/caps/cap_heap.h"
#include "04_analysis/caps/cap_concurrency.h"
#include "04_analysis/memory/region_type.h"
#include "04_analysis/typing/outcome.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsResolverModules() {
  SPEC_DEF("ResolveModules", "5.1.7");
  SPEC_DEF("ResolveModule", "5.1.7");
  SPEC_DEF("ResolveItems", "5.1.7");
  SPEC_DEF("ResolveItem", "5.1.7");
  SPEC_DEF("ValidateModulePath", "5.1.7");
  SPEC_DEF("ValidateModuleNames", "5.1.5");
  SPEC_DEF("CollectNames", "5.1.5");
  SPEC_DEF("TopLevelVis", "5.1.4");
  SPEC_DEF("BindSelfRecord", "5.1.7");
  SPEC_DEF("BindSelfClass", "5.1.7");
}

std::optional<std::string_view> CodeForResolveDiag(
    std::string_view diag_id) {
  if (diag_id == "ResolveExpr-Ident-Err" ||
      diag_id == "ResolveQual-Name-Err" ||
      diag_id == "ResolveQual-Apply-Err" ||
      diag_id == "ResolveQual-Apply-Brace-Err" ||
      diag_id == "Expr-Unresolved-Err") {
    return "E-MOD-1301";
  }
  if (diag_id == "E-CON-0031") {
    return "E-CON-0031";
  }
  if (diag_id == "ResolveModulePath-Err") {
    return "E-MOD-1304";
  }
  if (diag_id == "Import-Using-Missing") {
    return "E-MOD-1201";
  }
  if (diag_id == "Resolve-Import-Err") {
    return "E-MOD-1202";
  }
  if (diag_id == "Import-Using-Name-Conflict") {
    return "E-MOD-1203";
  }
  if (diag_id == "Resolve-Using-None") {
    return "E-MOD-1204";
  }
  if (diag_id == "Using-Path-Item-Public-Err" ||
      diag_id == "Using-List-Public-Err") {
    return "E-MOD-1205";
  }
  if (diag_id == "Using-List-Dup") {
    return "E-MOD-1206";
  }
  if (diag_id == "Resolve-Using-Ambig") {
    return "E-MOD-1208";
  }
  if (diag_id == "Validate-ModulePath-Reserved-Err") {
    return "E-CNF-0402";
  }
  if (diag_id == "Validate-Module-Keyword-Err") {
    return "E-CNF-0401";
  }
  if (diag_id == "Validate-Module-Prim-Shadow-Err") {
    return "E-MOD-1304";
  }
  if (diag_id == "Validate-Module-Special-Shadow-Err") {
    return "E-MOD-1304";
  }
  if (diag_id == "Validate-Module-Async-Shadow-Err") {
    return "E-MOD-1304";
  }
  if (diag_id == "Protected-TopLevel-Err") {
    return "E-MOD-2440";
  }
  if (diag_id == "Intro-Reserved-Gen-Err" ||
      diag_id == "Shadow-Reserved-Gen-Err") {
    return "E-CNF-0406";
  }
  if (diag_id == "Intro-Reserved-Cursive-Err" ||
      diag_id == "Shadow-Reserved-Cursive-Err") {
    return "E-CNF-0402";
  }
  if (diag_id == "Intro-Outer-Err") {
    return "E-MOD-1304";
  }
  if (diag_id == "Collect-Dup" || diag_id == "Names-Step-Dup") {
    return "E-MOD-1302";
  }
  if (diag_id == "E-MOD-2430") {
    return "E-MOD-2430";
  }
  if (diag_id == "Intro-Dup") {
    return "E-MOD-1302";
  }
  if (diag_id == "Shadow-Unnecessary") {
    return "E-MOD-1306";
  }
  if (diag_id == "Pat-Dup-Err") {
    return "E-SEM-2713";
  }
  if (diag_id == "Access-Err") {
    return "E-MOD-1207";
  }
  return std::nullopt;
}

std::optional<core::Span> SpanOfItem(const ast::ASTItem& item) {
  return std::visit(
      [](const auto& it) -> std::optional<core::Span> { return it.span; },
      item);
}

core::DiagnosticStream EmitResolveDiag(
    core::DiagnosticStream diags,
    std::string_view diag_id,
    const std::optional<core::Span>& span,
    const std::string& detail = {},
    const std::vector<core::SubDiagnostic>& children = {}) {
  const auto code = CodeForResolveDiag(diag_id);
  if (!code.has_value()) {
    core::Diagnostic diag;
    diag.severity = core::Severity::Error;
    diag.span = span;
    diag.message =
        "Internal error: resolver failed with unmapped diagnostic id `" +
        std::string(diag_id) + "`.";
    if (!detail.empty()) {
      core::SubDiagnostic note;
      note.kind = core::SubDiagnosticKind::Note;
      note.message = detail;
      diag.children.push_back(std::move(note));
    }
    diag.children.insert(diag.children.end(), children.begin(), children.end());
    core::Emit(diags, diag);
    return diags;
  }
  if (auto diag = core::MakeDiagnosticById(*code, span)) {
    if (!detail.empty()) {
      core::SubDiagnostic note;
      note.kind = core::SubDiagnosticKind::Note;
      note.message = detail;
      diag->children.push_back(std::move(note));
    }
    diag->children.insert(diag->children.end(), children.begin(),
                          children.end());
    core::Emit(diags, *diag);
    return diags;
  }
  core::Diagnostic diag;
  diag.severity = core::Severity::Error;
  diag.span = span;
  diag.message =
      "Internal error: resolver diagnostic id `" + std::string(diag_id) +
      "` mapped to unregistered diagnostic code `" + std::string(*code) + "`.";
  if (!detail.empty()) {
    core::SubDiagnostic note;
    note.kind = core::SubDiagnosticKind::Note;
    note.message = detail;
    diag.children.push_back(std::move(note));
  }
  diag.children.insert(diag.children.end(), children.begin(), children.end());
  core::Emit(diags, diag);
  return diags;
}

core::DiagnosticStream EmitInternalResolveFailure(
    core::DiagnosticStream diags,
    std::string message,
    const std::optional<core::Span>& span = std::nullopt) {
  core::Diagnostic diag;
  diag.severity = core::Severity::Error;
  diag.span = span;
  diag.message = std::move(message);
  core::Emit(diags, diag);
  return diags;
}

ast::Path FullPath(const ast::ModulePath& module_path,
                      std::string_view name) {
  ast::Path out = module_path;
  out.emplace_back(name);
  return out;
}

}  // namespace

void PopulateSigma(ScopeContext& ctx) {
  SpecDefsResolverModules();
  ctx.sigma.types.clear();
  ctx.sigma.classes.clear();

  auto make_type_prim = [](std::string_view name) -> std::shared_ptr<ast::Type> {
    auto ty = std::make_shared<ast::Type>();
    ty->node = ast::TypePrim{std::string(name)};
    return ty;
  };
  auto make_type_path = [](std::initializer_list<std::string_view> segs)
      -> std::shared_ptr<ast::Type> {
    ast::TypePathType path_type{};
    for (const auto seg : segs) {
      path_type.path.push_back(std::string(seg));
    }
    auto ty = std::make_shared<ast::Type>();
    ty->node = std::move(path_type);
    return ty;
  };
  auto make_type_dynamic = [](std::initializer_list<std::string_view> segs)
      -> std::shared_ptr<ast::Type> {
    ast::TypeDynamic dyn{};
    for (const auto seg : segs) {
      dyn.path.push_back(std::string(seg));
    }
    auto ty = std::make_shared<ast::Type>();
    ty->node = std::move(dyn);
    return ty;
  };
  auto make_type_perm = [&](ast::TypePerm perm, std::shared_ptr<ast::Type> base)
      -> std::shared_ptr<ast::Type> {
    ast::TypePermType perm_type{};
    perm_type.perm = perm;
    perm_type.base = std::move(base);
    auto ty = std::make_shared<ast::Type>();
    ty->node = std::move(perm_type);
    return ty;
  };
  auto make_type_union = [&](std::shared_ptr<ast::Type> lhs,
                             std::shared_ptr<ast::Type> rhs)
      -> std::shared_ptr<ast::Type> {
    ast::TypeUnion uni{};
    uni.types.push_back(std::move(lhs));
    uni.types.push_back(std::move(rhs));
    auto ty = std::make_shared<ast::Type>();
    ty->node = std::move(uni);
    return ty;
  };
  auto make_param = [](std::string_view name, std::shared_ptr<ast::Type> type) {
    ast::Param param{};
    param.name = std::string(name);
    param.type = std::move(type);
    return param;
  };

  // Built-in foundational classes
  {
    ast::Path path;
    path.emplace_back("Drop");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Drop";
    decl.supers = {};
    ast::ClassMethodDecl drop_method{};
    drop_method.vis = ast::Visibility::Public;
    drop_method.name = "drop";
    drop_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
    drop_method.params = {};
    drop_method.return_type_opt = make_type_prim("()");
    drop_method.body_opt = nullptr;
    decl.items = {drop_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Bitcopy");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Bitcopy";
    decl.supers = {};
    decl.items = {};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Clone");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Clone";
    decl.supers = {};
    ast::ClassMethodDecl clone_method{};
    clone_method.vis = ast::Visibility::Public;
    clone_method.name = "clone";
    clone_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    clone_method.params = {};
    clone_method.return_type_opt = make_type_path({"Self"});
    clone_method.body_opt = nullptr;
    decl.items = {clone_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Eq");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Eq";
    decl.supers = {};
    ast::ClassMethodDecl eq_method{};
    eq_method.vis = ast::Visibility::Public;
    eq_method.name = "eq";
    eq_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    eq_method.params = {
        make_param("other", make_type_perm(ast::TypePerm::Const, make_type_path({"Self"})))};
    eq_method.return_type_opt = make_type_prim("bool");
    eq_method.body_opt = nullptr;
    decl.items = {eq_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Hasher");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Hasher";
    decl.supers = {};
    ast::ClassMethodDecl write_method{};
    write_method.vis = ast::Visibility::Public;
    write_method.name = "write";
    write_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
    ast::TypeBytes bytes_view{};
    bytes_view.state = ast::BytesState::View;
    auto bytes_view_ty = std::make_shared<ast::Type>();
    bytes_view_ty->node = bytes_view;
    write_method.params = {make_param("data", bytes_view_ty)};
    write_method.return_type_opt = make_type_prim("()");
    write_method.body_opt = nullptr;

    ast::ClassMethodDecl finish_method{};
    finish_method.vis = ast::Visibility::Public;
    finish_method.name = "finish";
    finish_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    finish_method.params = {};
    finish_method.return_type_opt = make_type_prim("u64");
    finish_method.body_opt = nullptr;

    decl.items = {write_method, finish_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Hash");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Hash";
    decl.supers = {};
    ast::ClassMethodDecl hash_method{};
    hash_method.vis = ast::Visibility::Public;
    hash_method.name = "hash";
    hash_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    hash_method.params = {
        make_param("hasher", make_type_perm(ast::TypePerm::Unique, make_type_dynamic({"Hasher"})))};
    hash_method.return_type_opt = make_type_prim("()");
    hash_method.body_opt = nullptr;
    decl.items = {hash_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("FfiSafe");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "FfiSafe";
    decl.supers = {};
    decl.items = {};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Iterator");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Iterator";
    decl.supers = {};
    ast::AssociatedTypeDecl item_type{};
    item_type.vis = ast::Visibility::Public;
    item_type.name = "Item";
    item_type.default_type = nullptr;
    ast::ClassMethodDecl next_method{};
    next_method.vis = ast::Visibility::Public;
    next_method.name = "next";
    next_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Unique};
    next_method.params = {};
    next_method.return_type_opt =
        make_type_union(make_type_path({"Self", "Item"}), make_type_prim("()"));
    next_method.body_opt = nullptr;
    decl.items = {item_type, next_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }
  {
    ast::Path path;
    path.emplace_back("Step");
    ast::ClassDecl decl{};
    decl.vis = ast::Visibility::Public;
    decl.name = "Step";
    decl.supers = {};
    ast::ClassMethodDecl successor_method{};
    successor_method.vis = ast::Visibility::Public;
    successor_method.name = "successor";
    successor_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    successor_method.params = {};
    successor_method.return_type_opt =
        make_type_union(make_type_path({"Self"}), make_type_prim("()"));
    successor_method.body_opt = nullptr;
    ast::ClassMethodDecl predecessor_method{};
    predecessor_method.vis = ast::Visibility::Public;
    predecessor_method.name = "predecessor";
    predecessor_method.receiver = ast::ReceiverShorthand{ast::ReceiverPerm::Const};
    predecessor_method.params = {};
    predecessor_method.return_type_opt =
        make_type_union(make_type_path({"Self"}), make_type_prim("()"));
    predecessor_method.body_opt = nullptr;
    decl.items = {successor_method, predecessor_method};
    ctx.sigma.classes[PathKeyOf(path)] = decl;
  }

  // Built-in types: Region modal, RegionOptions, Context/System records,
  // CpuSet alias, and Priority enum.
  {
    ast::Path path;
    path.emplace_back("Region");
    ctx.sigma.types[PathKeyOf(path)] = BuildRegionModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("RegionOptions");
    ctx.sigma.types[PathKeyOf(path)] = BuildRegionOptionsRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Context");
    ctx.sigma.types[PathKeyOf(path)] = BuildContextRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("PanicRecord");
    ctx.sigma.types[PathKeyOf(path)] = BuildPanicRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("System");
    ctx.sigma.types[PathKeyOf(path)] = BuildSystemRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("CpuSet");
    ctx.sigma.types[PathKeyOf(path)] = BuildCpuSetAliasDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Priority");
    ctx.sigma.types[PathKeyOf(path)] = BuildPriorityEnumDecl();
  }

  // Built-in capability classes
  {
    ast::Path path;
    path.emplace_back("ExecutionDomain");
    ctx.sigma.classes[PathKeyOf(path)] = BuildExecutionDomainClassDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Reactor");
    ctx.sigma.classes[PathKeyOf(path)] = BuildReactorClassDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Async");
    ctx.sigma.classes[PathKeyOf(path)] = BuildAsyncClassDecl();
  }

  // Built-in error types
  {
    ast::Path path;
    path.emplace_back("AllocationError");
    ctx.sigma.types[PathKeyOf(path)] = BuildAllocationErrorEnumDecl();
  }
  {
    ast::Path path;
    path.emplace_back("IoError");
    ctx.sigma.types[PathKeyOf(path)] = BuildIoErrorEnumDecl();
  }
  {
    ast::Path path;
    path.emplace_back("FileKind");
    ctx.sigma.types[PathKeyOf(path)] = BuildFileKindEnumDecl();
  }
  {
    ast::Path path;
    path.emplace_back("DirEntry");
    ctx.sigma.types[PathKeyOf(path)] = BuildDirEntryRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("TypeCategory");
    ctx.sigma.types[PathKeyOf(path)] = BuildTypeCategoryEnumDecl();
  }
  {
    ast::Path path;
    path.emplace_back("FieldInfo");
    ctx.sigma.types[PathKeyOf(path)] = BuildFieldInfoRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("VariantInfo");
    ctx.sigma.types[PathKeyOf(path)] = BuildVariantInfoRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("StateInfo");
    ctx.sigma.types[PathKeyOf(path)] = BuildStateInfoRecordDecl();
  }
  {
    ast::Path path;
    path.emplace_back("SourceSpan");
    ctx.sigma.types[PathKeyOf(path)] = BuildSourceSpanRecordDecl();
  }

  // Built-in modal types
  {
    ast::Path path;
    path.emplace_back("File");
    ctx.sigma.types[PathKeyOf(path)] = BuildFileModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("DirIter");
    ctx.sigma.types[PathKeyOf(path)] = BuildDirIterModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Spawned");
    ctx.sigma.types[PathKeyOf(path)] = BuildSpawnedModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("CancelToken");
    ctx.sigma.types[PathKeyOf(path)] = BuildCancelTokenModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Async");
    ctx.sigma.types[PathKeyOf(path)] = BuildAsyncModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Outcome");
    ctx.sigma.types[PathKeyOf(path)] = BuildOutcomeModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Tracked");
    ctx.sigma.types[PathKeyOf(path)] = BuildTrackedModalDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Future");
    ctx.sigma.types[PathKeyOf(path)] = BuildFutureAliasDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Sequence");
    ctx.sigma.types[PathKeyOf(path)] = BuildSequenceAliasDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Stream");
    ctx.sigma.types[PathKeyOf(path)] = BuildStreamAliasDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Pipe");
    ctx.sigma.types[PathKeyOf(path)] = BuildPipeAliasDecl();
  }
  {
    ast::Path path;
    path.emplace_back("Exchange");
    ctx.sigma.types[PathKeyOf(path)] = BuildExchangeAliasDecl();
  }

  // Collect types and classes from all modules
  for (const auto& module : ctx.sigma.mods) {
    for (const auto& item : module.items) {
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::RecordDecl> ||
                          std::is_same_v<T, ast::EnumDecl> ||
                          std::is_same_v<T, ast::ModalDecl> ||
                          std::is_same_v<T, ast::TypeAliasDecl>) {
              ctx.sigma.types[PathKeyOf(FullPath(module.path, node.name))] = node;
            } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
              ctx.sigma.classes[PathKeyOf(FullPath(module.path, node.name))] = node;
            } else {
              return;
            }
          },
          item);
    }
  }
}

ResolveModulesResult ResolveModules(ResolveContext& ctx) {
  SpecDefsResolverModules();
  ResolveModulesResult result;
  if (!ctx.parse_ok ||
      (ctx.parse_diags != nullptr && core::HasError(*ctx.parse_diags))) {
    SPEC_RULE("ResolveModules-Err-Parse");
    if (ctx.parse_diags != nullptr) {
      result.diags = *ctx.parse_diags;
    }
    if (!core::HasError(result.diags)) {
      result.diags = EmitInternalResolveFailure(
          std::move(result.diags),
          "Internal error: resolver was asked to continue after parse failure "
          "without a parse diagnostic.");
    }
    return result;
  }
  if (!ctx.ctx) {
    result.diags = EmitInternalResolveFailure(
        std::move(result.diags),
        "Internal error: resolver failed without a scope context.");
    return result;
  }
  result.ok = true;
  bool had_resolve_error = false;
  const auto& modules = ctx.ctx->sigma.mods;
  result.modules.reserve(modules.size());
  for (const auto& module : modules) {
    ctx.ctx->current_module = module.path;
    const auto resolved = ResolveModule(ctx, module);
    if (!resolved.ok) {
      result.ok = false;
      if (!had_resolve_error) {
        SPEC_RULE("ResolveModules-Err-Resolve");
        had_resolve_error = true;
      }
      if (resolved.diag_id.has_value()) {
        result.diags = EmitResolveDiag(result.diags, *resolved.diag_id,
                                       resolved.span, resolved.diag_detail,
                                       resolved.diag_children);
      } else {
        core::Diagnostic diag;
        diag.severity = core::Severity::Error;
        diag.span = resolved.span;
        std::string module_path_text;
        for (std::size_t i = 0; i < module.path.size(); ++i) {
          if (i != 0) {
            module_path_text += "::";
          }
          module_path_text += module.path[i];
        }
        diag.message =
            "Internal error: module resolution failed without diagnostic for `" +
            module_path_text + "`.";
        if (!resolved.diag_detail.empty()) {
          core::SubDiagnostic note;
          note.kind = core::SubDiagnosticKind::Note;
          note.message = resolved.diag_detail;
          diag.children.push_back(std::move(note));
        }
        diag.children.insert(diag.children.end(), resolved.diag_children.begin(),
                             resolved.diag_children.end());
        core::Emit(result.diags, diag);
      }
    } else {
      result.modules.push_back(resolved.module);
    }
  }
  if (result.ok) {
    SPEC_RULE("ResolveModules-Ok");
  }
  return result;
}

ResolveModuleResult ResolveModule(ResolveContext& ctx,
                                  const ast::ASTModule& module) {
  SpecDefsResolverModules();
  SPEC_RULE("Res-Start");
  if (!ctx.ctx || !ctx.name_maps || !ctx.module_names) {
    return {};
  }

  const auto collected =
      CollectNames(*ctx.ctx, *ctx.name_maps, *ctx.module_names, module);
  if (!collected.ok) {
    SPEC_RULE("ResolveModule-Err");
    return {false, collected.diag_id, collected.span, {}};
  }

  SPEC_RULE("Res-Names");

  if (ReservedModulePath(module.path)) {
    SPEC_RULE("Validate-ModulePath-Reserved-Err");
    return {false, "Validate-ModulePath-Reserved-Err", std::nullopt, {}};
  }
  SPEC_RULE("Validate-ModulePath-Ok");

  const auto names_ok = ValidateModuleNames(collected.names, collected.name_spans);
  if (!names_ok.ok) {
    SPEC_RULE("ResolveModule-Err");
    return {false, names_ok.diag_id, names_ok.span, {}};
  }

  ScopedCurrentModuleOverride module_override(*ctx.ctx, module.path);
  ScopedScopesOverride scopes_override(
      *ctx.ctx, ScopeList{Scope{}, collected.names, UniverseBindings()});

  ResolveContext child = ctx;

  const auto items = ResolveItems(child, module.items);
  if (!items.ok) {
    SPEC_RULE("ResolveModule-Err");
    return {false, items.diag_id, items.span, {},
            items.diag_detail, items.diag_children};
  }

  SPEC_RULE("Res-Items");
  SPEC_RULE("ResolveModule-Ok");
  ast::ASTModule out = module;
  out.items = items.value;
  return {true, std::nullopt, std::nullopt, std::move(out)};
}

ResItemsResult ResolveItems(ResolveContext& ctx,
                            const std::vector<ast::ASTItem>& items) {
  SpecDefsResolverModules();
  SPEC_RULE("Res-Start");
  ResItemsResult result;
  if (items.empty()) {
    SPEC_RULE("ResolveItems-Empty");
    result.ok = true;
    return result;
  }
  result.value.reserve(items.size());
  for (const auto& item : items) {
    const auto vis = TopLevelVis(item);
    if (!vis.ok) {
      SPEC_RULE("ResolveItems-Err");
      return {false, vis.diag_id, SpanOfItem(item), {}};
    }
    const auto resolved = ResolveItem(ctx, item);
    if (!resolved.ok) {
      SPEC_RULE("ResolveItems-Err");
      return {false, resolved.diag_id, resolved.span, {},
              resolved.diag_detail, resolved.diag_children};
    }
    result.value.push_back(resolved.value);
    SPEC_RULE("ResolveItems-Cons");
  }
  result.ok = true;
  return result;
}

}  // namespace cursive::analysis
