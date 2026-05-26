#include "04_analysis/typing/outcome.h"

#include <string>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/typing/type_predicates.h"

namespace ultraviolet::analysis {

namespace {

static inline void SpecDefsOutcome() {
  SPEC_DEF("Outcome", "5.4");
  SPEC_DEF("OutcomeSig", "5.2.12");
  SPEC_DEF("OutcomeValue", "5.2.12");
  SPEC_DEF("OutcomeError", "5.2.12");
}

ast::TypePtr MakeTypeNode(ast::TypeNode node) {
  auto type = std::make_shared<ast::Type>();
  type->span = core::Span{};
  type->node = std::move(node);
  return type;
}

ast::TypePtr MakeTypePathAst(std::initializer_list<std::string_view> comps) {
  ast::TypePathType path_type;
  for (const auto comp : comps) {
    path_type.path.emplace_back(comp);
  }
  return MakeTypeNode(std::move(path_type));
}

ast::TypeParam MakeTypeParam(std::string_view name) {
  ast::TypeParam param{};
  param.name = std::string(name);
  param.bounds = {};
  param.default_type = nullptr;
  param.variance = std::nullopt;
  param.span = core::Span{};
  return param;
}

std::optional<ast::GenericParams> MakeGenericParams(
    std::initializer_list<ast::TypeParam> params) {
  ast::GenericParams generic_params{};
  generic_params.params.assign(params.begin(), params.end());
  generic_params.span = core::Span{};
  return generic_params;
}

ast::StateFieldDecl MakeStateField(std::string_view name, ast::TypePtr type) {
  ast::StateFieldDecl field{};
  field.vis = ast::Visibility::Public;
  field.name = std::string(name);
  field.type = std::move(type);
  field.span = core::Span{};
  field.doc_opt = std::nullopt;
  return field;
}

}  // namespace

bool IsOutcomeTypePath(const ast::TypePath& path) {
  SpecDefsOutcome();
  return PathMatchesBuiltinName(path, "Outcome");
}

TypeRef MakeOutcomeType(TypeRef value_type, TypeRef error_type) {
  SpecDefsOutcome();
  return MakeTypePath({"Outcome"}, {std::move(value_type), std::move(error_type)});
}

TypeRef MakeOutcomeStateType(TypeRef value_type,
                             TypeRef error_type,
                             std::string_view state) {
  SpecDefsOutcome();
  return MakeTypeModalState(
      {"Outcome"},
      std::string(state),
      {std::move(value_type), std::move(error_type)});
}

std::optional<OutcomeSig> OutcomeSigOf(const TypeRef& type) {
  SpecDefsOutcome();
  if (!type) {
    return std::nullopt;
  }

  TypeRef stripped = StripPerm(type);
  if (!stripped) {
    stripped = type;
  }

  const TypePath* path = nullptr;
  const std::vector<TypeRef>* args = nullptr;

  if (const auto* applied = std::get_if<TypePathType>(&stripped->node)) {
    path = &applied->path;
    args = &applied->generic_args;
  } else if (const auto* applied = std::get_if<TypeApply>(&stripped->node)) {
    path = &applied->path;
    args = &applied->args;
  } else if (const auto* state = std::get_if<TypeModalState>(&stripped->node)) {
    path = &state->path;
    args = &state->generic_args;
  }

  if (!path || !args || !IsOutcomeTypePath(*path) || args->size() != 2) {
    return std::nullopt;
  }

  return OutcomeSig{(*args)[0], (*args)[1]};
}

ast::ModalDecl BuildOutcomeModalDecl() {
  SpecDefsOutcome();
  ast::ModalDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Outcome";
  decl.generic_params = MakeGenericParams({
      MakeTypeParam("TValue"),
      MakeTypeParam("TError"),
  });
  decl.implements = {};

  ast::StateBlock value_state{};
  value_state.name = "Value";
  value_state.members = {
      MakeStateField("value", MakeTypePathAst({"TValue"})),
  };
  value_state.span = core::Span{};
  value_state.doc_opt = std::nullopt;
  decl.states.push_back(std::move(value_state));

  ast::StateBlock error_state{};
  error_state.name = "Error";
  error_state.members = {
      MakeStateField("error", MakeTypePathAst({"TError"})),
  };
  error_state.span = core::Span{};
  error_state.doc_opt = std::nullopt;
  decl.states.push_back(std::move(error_state));

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

}  // namespace ultraviolet::analysis
