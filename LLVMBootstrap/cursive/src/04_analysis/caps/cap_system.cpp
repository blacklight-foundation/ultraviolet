// =============================================================================
// MIGRATION MAPPING: cap_system.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
// - Section 5.9.4 "SystemInterface"
// - Section 5.9.4 "SystemMethodSig"
// - Section 5.9.4 "BuiltinRecord"
// - Section 5.9.4 "Fields(Context)"
//
// SOURCE FILE: cursive-bootstrap/src/03_analysis/caps/cap_system.cpp
// - Lines 1-178 (entire file)
//
// Key source functions migrated:
// - LookupSystemMethodSig (lines 99-116): System method signature lookup
// - LookupContextMethodSig (lines 119-146): Context method signature lookup
// - BuildContextRecordDecl (lines 148-163): Build Context record declaration
// - BuildSystemRecordDecl (lines 165-175): Build System record declaration
//
// Supporting helpers:
// - MakeTypeNode (lines 20-25): Create Type node wrapper
// - MakeTypePrimAst (lines 27-29): Create primitive type AST node
// - MakeTypeStringAst (lines 31-36): Create string type AST node
// - MakeTypeDynamicAst (lines 38-45): Create dynamic type AST node
// - MakeTypePathAst (lines 47-54): Create path type AST node
// - MakeParam (lines 56-63): Create parameter AST node
// - MakeField (lines 65-75): Create field declaration AST node
// - TypeUnit, TypeNever, TypeStringAny, Union2 (lines 77-95): Type helpers
//
// REFACTORING NOTES:
// 1. Namespace changed from cursive0 to cursive
// 2. AST namespace changed from syntax:: to ast::
// 3. Include paths updated to new structure
// 4. SPEC_DEF and SPEC_RULE annotations preserved
//
// =============================================================================

#include "04_analysis/caps/cap_system.h"

#include <array>
#include <memory>
#include <utility>

#include "00_core/assert_spec.h"
#include "00_core/span.h"
#include "04_analysis/resolve/scopes.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsCapSystem() {
  SPEC_DEF("SystemInterface", "5.9.4");
  SPEC_DEF("SystemMethodSig", "5.9.4");
  SPEC_DEF("BuiltinRecord", "5.9.4");
  SPEC_DEF("Fields(Context)", "5.9.4");
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

static std::shared_ptr<ast::Type> MakeTypeStringAst(
    std::optional<StringState> state) {
  // Convert from analysis::StringState to ast::StringState
  std::optional<ast::StringState> ast_state;
  if (state.has_value()) {
    switch (*state) {
      case StringState::Managed:
        ast_state = ast::StringState::Managed;
        break;
      case StringState::View:
        ast_state = ast::StringState::View;
        break;
    }
  }
  ast::TypeString node;
  node.state = ast_state;
  return MakeTypeNode(node);
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

static ast::Param MakeParam(std::string_view name,
                            std::shared_ptr<ast::Type> type) {
  ast::Param param{};
  param.mode = std::nullopt;
  param.name = ast::Identifier{std::string(name)};
  param.type = std::move(type);
  param.span = core::Span{};
  return param;
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

static ast::ExprPtr MakeLiteralExpr(lexer::TokenKind kind,
                                    std::string_view lexeme) {
  auto expr = std::make_shared<ast::Expr>();
  ast::LiteralExpr lit{};
  lit.literal.kind = kind;
  lit.literal.lexeme = std::string(lexeme);
  lit.literal.span = core::Span{};
  expr->span = core::Span{};
  expr->node = std::move(lit);
  return expr;
}

static ast::FieldDecl MakeFieldWithInit(std::string_view name,
                                        std::shared_ptr<ast::Type> type,
                                        ast::ExprPtr init) {
  ast::FieldDecl field = MakeField(name, std::move(type));
  field.init_opt = std::move(init);
  return field;
}

static TypeRef TypeNever() {
  return MakeTypePrim("!");
}

}  // namespace

std::optional<SystemMethodSig> LookupSystemMethodSig(std::string_view name) {
  SpecDefsCapSystem();
  SystemMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "exit")) {
    sig.params = {MakeParam("code", MakeTypePrimAst("i32"))};
    sig.ret = TypeNever();
    return sig;
  }
  if (IdEq(name, "get_env")) {
    sig.params = {MakeParam("key", MakeTypeStringAst(StringState::View))};
    sig.ret = MakeTypeString(StringState::View);
    return sig;
  }
  if (IdEq(name, "executable_path")) {
    sig.params = {};
    sig.ret = MakeTypeString(StringState::View);
    return sig;
  }
  if (IdEq(name, "argument_count")) {
    sig.params = {};
    sig.ret = MakeTypePrim("usize");
    return sig;
  }
  if (IdEq(name, "argument")) {
    sig.params = {MakeParam("index", MakeTypePrimAst("usize"))};
    sig.ret = MakeTypeString(StringState::View);
    return sig;
  }
  if (IdEq(name, "current_directory")) {
    sig.params = {};
    sig.ret = MakeTypeString(StringState::View);
    return sig;
  }
  if (IdEq(name, "run")) {
    sig.params = {MakeParam("command", MakeTypeStringAst(StringState::View))};
    sig.ret = MakeTypePrim("i32");
    return sig;
  }

  return std::nullopt;
}

// C0X Extension: Context method lookup for execution domains (Section 18.2)
std::optional<ContextMethodSig> LookupContextMethodSig(
    std::string_view name,
    std::optional<std::size_t> arg_count) {
  SpecDefsCapSystem();
  ContextMethodSig sig{};
  sig.recv_perm = Permission::Const;

  if (IdEq(name, "cpu")) {
    if (!arg_count.has_value() || *arg_count == 0) {
      sig.params = {};
      sig.ret = MakeTypeDynamic(TypePath{"CpuDomain"});
      return sig;
    }
    if (*arg_count == 1) {
      sig.params = {MakeParam("mask", MakeTypePathAst({"CpuSet"}))};
      sig.ret = MakeTypeDynamic(TypePath{"CpuDomain"});
      return sig;
    }
    if (*arg_count == 2) {
      sig.params = {
          MakeParam("mask", MakeTypePathAst({"CpuSet"})),
          MakeParam("prio", MakeTypePathAst({"Priority"})),
      };
      sig.ret = MakeTypeDynamic(TypePath{"CpuDomain"});
      return sig;
    }
    return std::nullopt;
  }
  if (IdEq(name, "gpu")) {
    if (arg_count.has_value() && *arg_count != 0) {
      return std::nullopt;
    }
    sig.params = {};
    sig.ret = MakeTypeDynamic(TypePath{"GpuDomain"});
    return sig;
  }
  if (IdEq(name, "inline")) {
    if (arg_count.has_value() && *arg_count != 0) {
      return std::nullopt;
    }
    sig.params = {};
    sig.ret = MakeTypeDynamic(TypePath{"InlineDomain"});
    return sig;
  }

  return std::nullopt;
}

ast::RecordDecl BuildContextRecordDecl() {
  SpecDefsCapSystem();
  ast::RecordDecl record;
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"Context"};
  record.generic_params = std::nullopt;
  record.implements = {ast::ClassPath{"Bitcopy"}};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeField("fs", MakeTypeDynamicAst({"FileSystem"})),
      MakeField("net", MakeTypeDynamicAst({"Network"})),
      MakeField("heap", MakeTypeDynamicAst({"HeapAllocator"})),
      MakeField("sys", MakeTypePathAst({"System"})),
      MakeField("reactor", MakeTypeDynamicAst({"Reactor"})),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

ast::RecordDecl BuildPanicRecordDecl() {
  SpecDefsCapSystem();
  ast::RecordDecl record;
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"PanicRecord"};
  record.generic_params = std::nullopt;
  record.implements = {ast::ClassPath{"Bitcopy"}};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeField("panic", MakeTypePrimAst("bool")),
      MakeField("code", MakeTypePrimAst("u32")),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

ast::RecordDecl BuildRegionOptionsRecordDecl() {
  SpecDefsCapSystem();
  ast::RecordDecl record;
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"RegionOptions"};
  record.generic_params = std::nullopt;
  record.implements = {};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {
      MakeFieldWithInit("stack_size",
                        MakeTypePrimAst("usize"),
                        MakeLiteralExpr(lexer::TokenKind::IntLiteral, "0usize")),
      MakeFieldWithInit("name",
                        MakeTypeStringAst(std::nullopt),
                        MakeLiteralExpr(lexer::TokenKind::StringLiteral, "\"\"")),
  };
  record.span = core::Span{};
  record.doc = {};
  return record;
}

ast::RecordDecl BuildSystemRecordDecl() {
  SpecDefsCapSystem();
  ast::RecordDecl record;
  record.attrs = {};
  record.vis = ast::Visibility::Public;
  record.name = ast::Identifier{"System"};
  record.generic_params = std::nullopt;
  record.implements = {ast::ClassPath{"Bitcopy"}};
  record.predicate_clause_opt = std::nullopt;
  record.invariant_opt = std::nullopt;
  record.members = {};
  record.span = core::Span{};
  record.doc = {};
  return record;
}

}  // namespace cursive::analysis
