#include "04_analysis/caps/cap_time.h"

#include <memory>
#include <string>
#include <utility>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/outcome.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCapTime() {
  SPEC_DEF("CapClass", "5.9.1");
  SPEC_DEF("CapType", "5.9.1");
  SPEC_DEF("CapMethodSig", "5.9.1");
  SPEC_DEF("CapRecv", "5.9.1");
  SPEC_DEF("TimeInterface", "14.9");
  SPEC_DEF("MonotonicTimeInterface", "14.9");
  SPEC_DEF("WallTimeInterface", "14.9");
  SPEC_DEF("TimeErrorDecl", "14.9");
  SPEC_DEF("DurationDecl", "14.9");
  SPEC_DEF("MonotonicInstantDecl", "14.9");
  SPEC_DEF("UtcInstantDecl", "14.9");
}

static ast::TypePtr MakeTypeNode(ast::TypeNode node) {
  auto ty = std::make_shared<ast::Type>();
  ty->span = core::Span{};
  ty->node = std::move(node);
  return ty;
}

static ast::TypePtr MakeTypePrimAst(std::string_view name) {
  return MakeTypeNode(ast::TypePrim{ast::Identifier{std::string(name)}});
}

static ast::TypePtr MakeTypePathAst(
    std::initializer_list<std::string_view> comps) {
  ast::TypePath path;
  for (const auto comp : comps) {
    path.emplace_back(comp);
  }
  return MakeTypeNode(ast::TypePathType{std::move(path), {}});
}

static ast::Param MakeParam(std::string_view name, ast::TypePtr type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = ast::Identifier{std::string(name)};
  param.type = std::move(type);
  param.span = core::Span{};
  return param;
}

static ast::FieldDecl MakeField(std::string_view name,
                                ast::TypePtr type,
                                ast::Visibility vis) {
  ast::FieldDecl field{};
  field.attrs = {};
  field.vis = vis;
  field.key_boundary = false;
  field.name = ast::Identifier{std::string(name)};
  field.type = std::move(type);
  field.init_opt = nullptr;
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

static TypeRef TypeDuration() {
  return MakeTypePath({"Duration"});
}

static TypeRef TypeMonotonicInstant() {
  return MakeTypePath({"MonotonicInstant"});
}

static TypeRef TypeUtcInstant() {
  return MakeTypePath({"UtcInstant"});
}

static TypeRef TypeTimeError() {
  return MakeTypePath({"TimeError"});
}

static TypeRef TypeMonotonicTimeDynamic() {
  return MakeTypeDynamic({"MonotonicTime"});
}

static TypeRef TypeWallTimeDynamic() {
  return MakeTypeDynamic({"WallTime"});
}

static TypeRef Outcome(TypeRef value, TypeRef error) {
  return MakeOutcomeType(std::move(value), std::move(error));
}

}  // namespace

bool IsTimeBuiltinTypePath(const ast::TypePath& path) {
  SpecDefsCapTime();
  return PathMatchesBuiltinName(path, "Duration") ||
         PathMatchesBuiltinName(path, "MonotonicInstant") ||
         PathMatchesBuiltinName(path, "UtcInstant") ||
         PathMatchesBuiltinName(path, "TimeError");
}

bool IsTimeClassPath(const ast::ClassPath& path) {
  SpecDefsCapTime();
  return PathMatchesBuiltinName(path, "Time");
}

bool IsMonotonicTimeClassPath(const ast::ClassPath& path) {
  SpecDefsCapTime();
  return PathMatchesBuiltinName(path, "MonotonicTime");
}

bool IsWallTimeClassPath(const ast::ClassPath& path) {
  SpecDefsCapTime();
  return PathMatchesBuiltinName(path, "WallTime");
}

std::optional<TimeMethodSig> LookupTimeMethodSig(std::string_view name) {
  SpecDefsCapTime();
  TimeMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "monotonic")) {
    sig.params = {};
    sig.ret = TypeMonotonicTimeDynamic();
    return sig;
  }
  if (IdEq(name, "wall")) {
    sig.params = {};
    sig.ret = TypeWallTimeDynamic();
    return sig;
  }

  return std::nullopt;
}

std::optional<TimeMethodSig> LookupMonotonicTimeMethodSig(
    std::string_view name) {
  SpecDefsCapTime();
  TimeMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "now")) {
    sig.params = {};
    sig.ret = TypeMonotonicInstant();
    return sig;
  }
  if (IdEq(name, "resolution")) {
    sig.params = {};
    sig.ret = TypeDuration();
    return sig;
  }
  if (IdEq(name, "elapsed")) {
    sig.params = {
        MakeParam("start", MakeTypePathAst({"MonotonicInstant"})),
        MakeParam("end", MakeTypePathAst({"MonotonicInstant"})),
    };
    sig.ret = Outcome(TypeDuration(), TypeTimeError());
    return sig;
  }
  if (IdEq(name, "coarsen")) {
    sig.params = {MakeParam("resolution", MakeTypePathAst({"Duration"}))};
    sig.ret = Outcome(TypeMonotonicTimeDynamic(), TypeTimeError());
    return sig;
  }

  return std::nullopt;
}

std::optional<TimeMethodSig> LookupWallTimeMethodSig(std::string_view name) {
  SpecDefsCapTime();
  TimeMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "now_utc")) {
    sig.params = {};
    sig.ret = Outcome(TypeUtcInstant(), TypeTimeError());
    return sig;
  }
  if (IdEq(name, "resolution")) {
    sig.params = {};
    sig.ret = Outcome(TypeDuration(), TypeTimeError());
    return sig;
  }
  if (IdEq(name, "coarsen")) {
    sig.params = {MakeParam("resolution", MakeTypePathAst({"Duration"}))};
    sig.ret = Outcome(TypeWallTimeDynamic(), TypeTimeError());
    return sig;
  }

  return std::nullopt;
}

ast::EnumDecl BuildTimeErrorEnumDecl() {
  SpecDefsCapTime();
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "TimeError";
  decl.implements = {};
  decl.span = core::Span{};
  decl.doc = {};

  auto make_variant = [](std::string_view name) {
    ast::VariantDecl variant{};
    variant.name = std::string(name);
    variant.payload_opt = std::nullopt;
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;
    return variant;
  };

  decl.variants = {
      make_variant("Unsupported"),
      make_variant("ClockUnavailable"),
      make_variant("OutOfRange"),
      make_variant("InvalidResolution"),
      make_variant("ClockMismatch"),
  };
  return decl;
}

ast::RecordDecl BuildDurationRecordDecl() {
  SpecDefsCapTime();
  ast::RecordDecl record{};
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"Duration"};
  record.generic_params = std::nullopt;
  record.implements = {};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeField("nanoseconds", MakeTypePrimAst("u128"), ast::Visibility::Public),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

ast::RecordDecl BuildMonotonicInstantRecordDecl() {
  SpecDefsCapTime();
  ast::RecordDecl record{};
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"MonotonicInstant"};
  record.generic_params = std::nullopt;
  record.implements = {};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeField("domain", MakeTypePrimAst("usize"), ast::Visibility::Private),
      MakeField("ticks", MakeTypePrimAst("u128"), ast::Visibility::Private),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

ast::RecordDecl BuildUtcInstantRecordDecl() {
  SpecDefsCapTime();
  ast::RecordDecl record{};
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"UtcInstant"};
  record.generic_params = std::nullopt;
  record.implements = {};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeField("unix_nanoseconds",
                MakeTypePrimAst("i128"),
                ast::Visibility::Public),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

}  // namespace cursive::analysis
