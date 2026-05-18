#include "04_analysis/caps/cap_comptime.h"

#include <memory>
#include <string_view>

namespace ultraviolet::analysis {

namespace {

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
  ast::TypePathType path_type;
  for (const auto comp : comps) {
    path_type.path.emplace_back(comp);
  }
  return MakeTypeNode(path_type);
}

static std::shared_ptr<ast::Type> MakeTypeStringAst(
    std::optional<ast::StringState> state) {
  ast::TypeString node;
  node.state = state;
  return MakeTypeNode(node);
}

static std::shared_ptr<ast::Type> MakeTypeSliceAst(
    std::shared_ptr<ast::Type> element) {
  ast::TypeSlice node;
  node.element = std::move(element);
  return MakeTypeNode(node);
}

static ast::FieldDecl MakeField(std::string_view name,
                                std::shared_ptr<ast::Type> type) {
  ast::FieldDecl field{};
  field.vis = ast::Visibility::Public;
  field.name = std::string(name);
  field.type = std::move(type);
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

}  // namespace

ast::RecordDecl BuildSourceSpanRecordDecl() {
  ast::RecordDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "SourceSpan";
  decl.members.emplace_back(
      MakeField("file", MakeTypeStringAst(ast::StringState::Managed)));
  decl.members.emplace_back(MakeField("start_line", MakeTypePrimAst("usize")));
  decl.members.emplace_back(MakeField("start_col", MakeTypePrimAst("usize")));
  decl.members.emplace_back(MakeField("end_line", MakeTypePrimAst("usize")));
  decl.members.emplace_back(MakeField("end_col", MakeTypePrimAst("usize")));
  decl.span = core::Span{};
  return decl;
}

ast::EnumDecl BuildTypeCategoryEnumDecl() {
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "TypeCategory";
  decl.implements = {{"Bitcopy"}};
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
      make_variant("Record"),    make_variant("Enum"),
      make_variant("Modal"),     make_variant("Primitive"),
      make_variant("Tuple"),     make_variant("Array"),
      make_variant("Slice"),     make_variant("Union"),
      make_variant("Procedure"), make_variant("Reference"),
      make_variant("Dynamic"),   make_variant("Opaque"),
      make_variant("Generic"),   make_variant("String"),
      make_variant("Bytes"),     make_variant("Range"),
  };
  return decl;
}

ast::RecordDecl BuildFieldInfoRecordDecl() {
  ast::RecordDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "FieldInfo";
  decl.members = {
      MakeField("name", MakeTypeStringAst(ast::StringState::Managed)),
      MakeField("type", MakeTypePathAst({"Type"})),
      MakeField("visibility", MakeTypeStringAst(ast::StringState::Managed)),
      MakeField("index", MakeTypePrimAst("usize")),
      MakeField("span", MakeTypePathAst({"SourceSpan"})),
  };
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::RecordDecl BuildVariantInfoRecordDecl() {
  ast::RecordDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "VariantInfo";
  decl.members = {
      MakeField("name", MakeTypeStringAst(ast::StringState::Managed)),
      MakeField("payload_kind", MakeTypeStringAst(ast::StringState::Managed)),
      MakeField("payload_types", MakeTypeSliceAst(MakeTypePathAst({"Type"}))),
      MakeField("field_names",
                MakeTypeSliceAst(
                    MakeTypeStringAst(ast::StringState::Managed))),
      MakeField("span", MakeTypePathAst({"SourceSpan"})),
  };
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

ast::RecordDecl BuildStateInfoRecordDecl() {
  ast::RecordDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "StateInfo";
  decl.members = {
      MakeField("name", MakeTypeStringAst(ast::StringState::Managed)),
      MakeField("field_names",
                MakeTypeSliceAst(
                    MakeTypeStringAst(ast::StringState::Managed))),
      MakeField("method_names",
                MakeTypeSliceAst(
                    MakeTypeStringAst(ast::StringState::Managed))),
      MakeField("transition_names",
                MakeTypeSliceAst(
                    MakeTypeStringAst(ast::StringState::Managed))),
      MakeField("span", MakeTypePathAst({"SourceSpan"})),
  };
  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

}  // namespace ultraviolet::analysis
