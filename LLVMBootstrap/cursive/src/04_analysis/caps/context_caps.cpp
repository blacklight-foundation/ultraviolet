// =============================================================================
// context_caps.cpp - Context Record and Main Signature Validation
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 5.9.4 "Context Record" (lines 13202-13247)
//   - Context fields: fs ($FileSystem), net ($Network), heap ($HeapAllocator),
//                     sys (System), reactor ($Reactor)
//   - Context methods: cpu(), gpu(), inline() -> $ExecutionDomain
//   - Main signature: public procedure main(move ctx: Context) -> i32
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/caps/cap_system.cpp
//
// FUNCTIONS IMPLEMENTED:
//   - BuildContextRecord() -> Context record AST declaration
//   - ContextFieldType() -> Get type of Context field
//   - ContextMethodSignature() -> Get signature of Context method
//   - ValidateMainSignature() -> Validate main procedure signature
//   - IsContextTypePath() -> Check if path is "Context"
//   - IsSystemTypePath() -> Check if path is "System"
//
// =============================================================================

#include "04_analysis/caps/cap_system.h"

#include <array>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/resolve/scopes_lookup.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsContextCaps() {
  SPEC_DEF("ContextRecord", "5.9.4");
  SPEC_DEF("ContextFields", "5.9.4");
  SPEC_DEF("ContextMethods", "5.9.4");
  SPEC_DEF("MainSignature", "5.9.4");
  SPEC_DEF("CpuSet", "5.9.4");
  SPEC_DEF("Priority", "5.9.4");
}

static constexpr std::array<std::string_view, 7> kBuiltinRecordNames = {
    "RegionOptions",
    "DirEntry",
    "Context",
    "System",
    "Duration",
    "MonotonicInstant",
    "UtcInstant",
};

static std::optional<ast::Path> BuiltinRecordPath(std::string_view builtin_name) {
  for (const auto candidate : kBuiltinRecordNames) {
    if (IdEq(builtin_name, candidate)) {
      ast::Path record_path;
      record_path.emplace_back(std::string(candidate));
      return record_path;
    }
  }

  return std::nullopt;
}

static std::optional<ast::Path> BuiltinRecordPath(const ast::TypePath& path) {
  for (const auto candidate : kBuiltinRecordNames) {
    if (PathMatchesBuiltinName(path, candidate)) {
      ast::Path record_path;
      record_path.emplace_back(std::string(candidate));
      return record_path;
    }
  }

  return std::nullopt;
}

static std::shared_ptr<ast::Type> MakeTypeNode(const ast::TypeNode& node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = node;
  return ty;
}

static std::shared_ptr<ast::Type> MakeTypePrimAst(std::string_view name) {
  return MakeTypeNode(ast::TypePrim{ast::Identifier{std::string(name)}});
}

static std::shared_ptr<ast::Type> MakeTypeDynamicAst(
    std::initializer_list<std::string_view> comps) {
  ast::TypeDynamic node;
  for (const auto comp : comps) {
    node.path.emplace_back(comp);
  }
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypePathAst(
    std::initializer_list<std::string_view> comps) {
  ast::TypePath path;
  for (const auto comp : comps) {
    path.emplace_back(comp);
  }
  return MakeTypeNode(ast::TypePathType{std::move(path), {}});
}

static ast::FieldDecl MakeField(std::string_view name,
                                std::shared_ptr<ast::Type> type) {
  ast::FieldDecl field{};
  field.attrs = {};
  field.vis = ast::Visibility::Public;
  field.key_boundary = false;
  field.name = ast::Identifier{std::string(name)};
  field.type = std::move(type);
  field.init_opt = nullptr;
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

static TypeRef TypeUnit() {
  return MakeTypePrim("()");
}

static TypeRef TypeI32() {
  return MakeTypePrim("i32");
}

static std::string TypePathKeyString(const ast::TypePath& path) {
  if (path.empty()) {
    return {};
  }
  std::string out = path.front();
  for (std::size_t i = 1; i < path.size(); ++i) {
    out.append("::");
    out.append(path[i]);
  }
  return out;
}

static const ast::Type* StripAstPermAndRefine(const ast::Type& type) {
  const ast::Type* cur = &type;
  while (cur) {
    if (const auto* perm = std::get_if<ast::TypePermType>(&cur->node)) {
      cur = perm->base.get();
      continue;
    }
    if (const auto* refine = std::get_if<ast::TypeRefine>(&cur->node)) {
      cur = refine->base.get();
      continue;
    }
    break;
  }
  return cur;
}

struct ResolvedTypeDecl {
  const TypeDecl* decl = nullptr;
  ast::TypePath path;
};

static ResolvedTypeDecl ResolveVisibleTypeDecl(const ScopeContext& ctx,
                                               const ast::TypePath& path) {
  ResolvedTypeDecl result;
  if (path.empty()) {
    return result;
  }

  ast::Path direct(path.begin(), path.end());
  if (const auto it = ctx.sigma.types.find(PathKeyOf(direct));
      it != ctx.sigma.types.end()) {
    result.decl = &it->second;
    result.path = path;
    return result;
  }

  if (path.size() != 1) {
    return result;
  }

  const auto ent = ResolveTypeName(ctx, path.front());
  if (!ent.has_value() || !ent->origin_opt.has_value()) {
    return result;
  }

  ast::TypePath resolved = *ent->origin_opt;
  resolved.push_back(ent->target_opt.value_or(path.front()));
  const auto resolved_it = ctx.sigma.types.find(PathKeyOf(resolved));
  if (resolved_it == ctx.sigma.types.end()) {
    return result;
  }

  result.decl = &resolved_it->second;
  result.path = std::move(resolved);
  return result;
}

static bool MatchesDynamicBuiltinType(const ast::Type& type,
                                      std::string_view builtin_name) {
  const auto* stripped = StripAstPermAndRefine(type);
  const auto* dyn = stripped ? std::get_if<ast::TypeDynamic>(&stripped->node)
                             : nullptr;
  return dyn && PathMatchesBuiltinName(dyn->path, builtin_name);
}

static bool MatchesNamedBuiltinType(const ast::Type& type,
                                    std::string_view builtin_name) {
  const auto* stripped = StripAstPermAndRefine(type);
  const auto* path = stripped ? std::get_if<ast::TypePathType>(&stripped->node)
                              : nullptr;
  return path && path->generic_args.empty() &&
         PathMatchesBuiltinName(path->path, builtin_name);
}

static bool MatchesContextBundleFieldType(const ast::Type& type,
                                          std::string_view field_name) {
  if (IdEq(field_name, "fs")) {
    return MatchesDynamicBuiltinType(type, "FileSystem");
  }
  if (IdEq(field_name, "net")) {
    return MatchesDynamicBuiltinType(type, "Network");
  }
  if (IdEq(field_name, "heap")) {
    return MatchesDynamicBuiltinType(type, "HeapAllocator");
  }
  if (IdEq(field_name, "sys")) {
    return MatchesNamedBuiltinType(type, "System");
  }
  if (IdEq(field_name, "reactor")) {
    return MatchesDynamicBuiltinType(type, "Reactor");
  }
  if (IdEq(field_name, "time")) {
    return MatchesDynamicBuiltinType(type, "Time");
  }
  if (IdEq(field_name, "cpu") || IdEq(field_name, "gpu") ||
      IdEq(field_name, "inline")) {
    return MatchesDynamicBuiltinType(type, "ExecutionDomain");
  }
  return false;
}

static bool IsContextBundleTypeImpl(const ScopeContext& ctx,
                                    const ast::Type& type,
                                    std::unordered_set<std::string>& visiting) {
  const ast::Type* stripped = StripAstPermAndRefine(type);
  if (!stripped) {
    return false;
  }

  const auto* path = std::get_if<ast::TypePathType>(&stripped->node);
  if (!path || !path->generic_args.empty()) {
    return false;
  }
  if (IsContextTypePath(path->path)) {
    return true;
  }

  const auto resolved = ResolveVisibleTypeDecl(ctx, path->path);
  if (!resolved.decl) {
    return false;
  }
  const std::string visit_key = TypePathKeyString(resolved.path);
  if (!visiting.insert(visit_key).second) {
    return false;
  }

  const bool ok = std::visit(
      [&](const auto& decl) -> bool {
        using D = std::decay_t<decltype(decl)>;
        if constexpr (std::is_same_v<D, ast::TypeAliasDecl>) {
          if (!ast::TypeParamsOpt(decl.generic_params).empty()) {
            return false;
          }
          return decl.type && IsContextBundleTypeImpl(ctx, *decl.type, visiting);
        } else if constexpr (std::is_same_v<D, ast::RecordDecl>) {
          for (const auto& member : decl.members) {
            const auto* field = std::get_if<ast::FieldDecl>(&member);
            if (!field) {
              continue;
            }
            if (!field->type) {
              return false;
            }
            if (MatchesContextBundleFieldType(*field->type, field->name)) {
              continue;
            }
            if (!IsContextBundleTypeImpl(ctx, *field->type, visiting)) {
              return false;
            }
          }
          return true;
        } else {
          return false;
        }
      },
      *resolved.decl);

  visiting.erase(visit_key);
  return ok;
}

static bool IsAliasNormalizedContextTypeImpl(
    const ScopeContext& ctx,
    const ast::Type& type,
    std::unordered_set<std::string>& visiting) {
  const ast::Type* stripped = StripAstPermAndRefine(type);
  const auto* path = stripped ? std::get_if<ast::TypePathType>(&stripped->node)
                              : nullptr;
  if (!path || !path->generic_args.empty()) {
    return false;
  }
  if (IsContextTypePath(path->path)) {
    return true;
  }

  const auto resolved = ResolveVisibleTypeDecl(ctx, path->path);
  if (!resolved.decl) {
    return false;
  }
  const std::string visit_key = TypePathKeyString(resolved.path);
  if (!visiting.insert(visit_key).second) {
    return false;
  }

  bool is_context = false;
  if (const auto* alias = std::get_if<ast::TypeAliasDecl>(resolved.decl)) {
    if (ast::TypeParamsOpt(alias->generic_params).empty()) {
      is_context =
          alias->type &&
          IsAliasNormalizedContextTypeImpl(ctx, *alias->type, visiting);
    }
  }

  visiting.erase(visit_key);
  return is_context;
}

static bool IsAliasNormalizedContextType(const ScopeContext& ctx,
                                         const ast::Type& type) {
  std::unordered_set<std::string> visiting;
  return IsAliasNormalizedContextTypeImpl(ctx, type, visiting);
}

}  // namespace

// =============================================================================
// Context field type lookup
// =============================================================================

bool IsContextTypePath(const ast::TypePath& path) {
  SpecDefsContextCaps();
  return PathMatchesBuiltinName(path, "Context");
}

bool IsSystemTypePath(const ast::TypePath& path) {
  SpecDefsContextCaps();
  return PathMatchesBuiltinName(path, "System");
}

bool IsRegionOptionsTypePath(const ast::TypePath& path) {
  SpecDefsContextCaps();
  return LookupBuiltinRecordCtorPath(path).has_value();
}

std::optional<ast::Path> LookupBuiltinRecordCtorPath(const ast::TypePath& path) {
  SpecDefsContextCaps();
  return BuiltinRecordPath(path);
}

std::optional<ast::Path> LookupBuiltinRecordCtorPath(std::string_view ident) {
  SpecDefsContextCaps();
  return BuiltinRecordPath(ident);
}

std::optional<TypeRef> ContextFieldType(std::string_view field_name) {
  SpecDefsContextCaps();

  // Section 5.9.4: ContextFields
  if (IdEq(field_name, "fs")) {
    return MakeTypeDynamic({"FileSystem"});
  }
  if (IdEq(field_name, "net")) {
    return MakeTypeDynamic({"Network"});
  }
  if (IdEq(field_name, "heap")) {
    return MakeTypeDynamic({"HeapAllocator"});
  }
  if (IdEq(field_name, "sys")) {
    return MakeTypePath({"System"});
  }
  if (IdEq(field_name, "reactor")) {
    return MakeTypeDynamic({"Reactor"});
  }
  if (IdEq(field_name, "time")) {
    return MakeTypeDynamic({"Time"});
  }

  return std::nullopt;
}

std::optional<TypeRef> ContextBundleFieldType(std::string_view field_name) {
  SpecDefsContextCaps();

  if (IdEq(field_name, "cpu") || IdEq(field_name, "gpu") ||
      IdEq(field_name, "inline")) {
    return MakeTypeDynamic({"ExecutionDomain"});
  }
  return ContextFieldType(field_name);
}

bool IsContextBundleType(const ScopeContext& ctx, const ast::Type& type) {
  SpecDefsContextCaps();
  std::unordered_set<std::string> visiting;
  return IsContextBundleTypeImpl(ctx, type, visiting);
}

bool IsHostedContextBundleType(const ScopeContext& ctx, const ast::Type& type) {
  SpecDefsContextCaps();
  if (IsAliasNormalizedContextType(ctx, type)) {
    return false;
  }
  return IsContextBundleType(ctx, type);
}

// =============================================================================
// Main procedure signature validation
// =============================================================================

MainSignatureResult ValidateMainSignature(const ScopeContext& ctx,
                                          const ast::ProcedureDecl& proc) {
  SpecDefsContextCaps();
  MainSignatureResult result{};
  result.valid = false;

  // Must be named "main"
  if (!IdEq(proc.name, "main")) {
    result.error_code = "E-MOD-2431";
    result.error_message = "Entry point must be named 'main'";
    return result;
  }

  // Must be public
  if (proc.vis != ast::Visibility::Public) {
    result.error_code = "E-MOD-2431";
    result.error_message = "main procedure must be public";
    return result;
  }

  // Must take exactly one parameter
  if (proc.params.size() != 1) {
    result.error_code = "E-MOD-2431";
    result.error_message =
        "main procedure must take exactly one parameter";
    return result;
  }

  // Parameter must be 'move' mode or omitted.
  const auto& param = proc.params[0];
  if (param.mode.has_value() && *param.mode != ast::ParamMode::Move) {
    result.error_code = "E-MOD-2431";
    result.error_message =
        "main procedure parameter may only use 'move' mode";
    return result;
  }

  // Parameter type must be a Context bundle.
  if (param.type) {
    if (!IsContextBundleType(ctx, *param.type)) {
      result.error_code = "E-MOD-2431";
      result.error_message =
          "main procedure parameter must have a Context bundle type";
      return result;
    }
  } else {
    result.error_code = "E-MOD-2431";
    result.error_message = "main procedure parameter must have a Context bundle type";
    return result;
  }

  // Return type must be i32
  if (proc.return_type_opt) {
    const auto* ret_prim =
        std::get_if<ast::TypePrim>(&proc.return_type_opt->node);
    if (!ret_prim || !IdEq(ret_prim->name, "i32")) {
      result.error_code = "E-MOD-2431";
      result.error_message = "main procedure must return i32";
      return result;
    }
  } else {
    result.error_code = "E-MOD-2431";
    result.error_message = "main procedure must return i32";
    return result;
  }

  result.valid = true;
  return result;
}

// =============================================================================
// Built-in type declarations: CpuSet and Priority
// =============================================================================

ast::TypeAliasDecl BuildCpuSetAliasDecl() {
  SpecDefsContextCaps();
  ast::TypeAliasDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = ast::Identifier{"CpuSet"};
  decl.generic_params = std::nullopt;
  decl.predicate_clause_opt = std::nullopt;
  decl.type = MakeTypePrimAst("u64");
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::EnumDecl BuildPriorityEnumDecl() {
  SpecDefsContextCaps();
  ast::EnumDecl decl{};
  decl.attrs = {};
  decl.vis = ast::Visibility::Public;
  decl.name = ast::Identifier{"Priority"};
  decl.generic_params = std::nullopt;
  decl.implements = {ast::ClassPath{"Bitcopy"}};
  decl.predicate_clause_opt = std::nullopt;
  decl.invariant_opt = std::nullopt;
  decl.span = core::Span{};
  decl.doc = {};

  auto make_variant = [](std::string_view name) {
    ast::VariantDecl variant{};
    variant.name = ast::Identifier{std::string(name)};
    variant.payload_opt = std::nullopt;
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;
    return variant;
  };

  decl.variants = {make_variant("Low"), make_variant("Normal"),
                   make_variant("High")};
  return decl;
}

bool IsCapabilityClassPath(const ast::ClassPath& path) {
  SpecDefsContextCaps();
  return PathMatchesBuiltinName(path, "FileSystem") ||
         PathMatchesBuiltinName(path, "Network") ||
         PathMatchesBuiltinName(path, "HeapAllocator") ||
         PathMatchesBuiltinName(path, "ExecutionDomain") ||
         PathMatchesBuiltinName(path, "Reactor") ||
         PathMatchesBuiltinName(path, "Time") ||
         PathMatchesBuiltinName(path, "MonotonicTime") ||
         PathMatchesBuiltinName(path, "WallTime") ||
         PathMatchesBuiltinName(path, "System");
}

// =============================================================================
// FFI capability isolation check
// =============================================================================

bool TypeContainsCapability(const TypeRef& type) {
  SpecDefsContextCaps();
  if (!type) {
    return false;
  }

  // Direct capability type
  if (IsCapabilityType(type)) {
    return true;
  }

  // Check nested types
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePerm>) {
          return TypeContainsCapability(node.base);
        } else if constexpr (std::is_same_v<T, TypeUnion>) {
          for (const auto& member : node.members) {
            if (TypeContainsCapability(member)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeFunc>) {
          for (const auto& param : node.params) {
            if (TypeContainsCapability(param.type)) {
              return true;
            }
          }
          return TypeContainsCapability(node.ret);
        } else if constexpr (std::is_same_v<T, TypeClosure>) {
          for (const auto& param : node.params) {
            if (TypeContainsCapability(param.second)) {
              return true;
            }
          }
          if (TypeContainsCapability(node.ret)) {
            return true;
          }
          if (node.deps_opt.has_value()) {
            for (const auto& dep : *node.deps_opt) {
              if (TypeContainsCapability(dep.type)) {
                return true;
              }
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeTuple>) {
          for (const auto& elem : node.elements) {
            if (TypeContainsCapability(elem)) {
              return true;
            }
          }
          return false;
        } else if constexpr (std::is_same_v<T, TypeArray>) {
          return TypeContainsCapability(node.element);
        } else if constexpr (std::is_same_v<T, TypeSlice>) {
          return TypeContainsCapability(node.element);
        } else if constexpr (std::is_same_v<T, TypePtr>) {
          return TypeContainsCapability(node.element);
        } else if constexpr (std::is_same_v<T, TypeRefine>) {
          return TypeContainsCapability(node.base);
        } else {
          return false;
        }
      },
      type->node);
}

}  // namespace cursive::analysis
