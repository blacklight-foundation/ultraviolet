#include "04_analysis/typing/outcome.h"

#include <string>
#include <utility>
#include <vector>

#include "00_core/assert_spec.h"
#include "04_analysis/caps/builtin_paths.h"
#include "04_analysis/typing/subtyping.h"
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

}  // namespace

bool IsOutcomeTypePath(const ast::TypePath& path) {
  SpecDefsOutcome();
  return PathMatchesBuiltinName(path, "Outcome");
}

TypeRef MakeOutcomeType(TypeRef value_type, TypeRef error_type) {
  SpecDefsOutcome();
  return MakeTypePath({"Outcome"}, {std::move(value_type), std::move(error_type)});
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
  }

  if (!path || !args || !IsOutcomeTypePath(*path) || args->size() != 2) {
    return std::nullopt;
  }

  return OutcomeSig{(*args)[0], (*args)[1]};
}

ast::EnumDecl BuildOutcomeEnumDecl() {
  SpecDefsOutcome();
  ast::EnumDecl decl{};
  decl.vis = ast::Visibility::Public;
  decl.name = "Outcome";
  decl.generic_params = MakeGenericParams({
      MakeTypeParam("TValue"),
      MakeTypeParam("TError"),
  });
  decl.implements = {};

  auto make_variant = [](std::string_view name, ast::TypePtr payload_type) {
    ast::VariantDecl variant{};
    variant.name = std::string(name);
    ast::VariantPayloadTuple tuple{};
    tuple.elements.push_back(std::move(payload_type));
    variant.payload_opt = ast::VariantPayload{std::move(tuple)};
    variant.discriminant_opt = std::nullopt;
    variant.span = core::Span{};
    variant.doc_opt = std::nullopt;
    return variant;
  };

  decl.variants.push_back(make_variant("Value", MakeTypePathAst({"TValue"})));
  decl.variants.push_back(make_variant("Error", MakeTypePathAst({"TError"})));

  decl.span = core::Span{};
  decl.doc = {};
  return decl;
}

OutcomeIntro ClassifyOutcomeIntro(const ScopeContext& ctx,
                                  const TypeRef& from,
                                  const TypeRef& to) {
  SpecDefsOutcome();
  const auto sig = OutcomeSigOf(to);
  if (!sig || !from) {
    return OutcomeIntro::None;
  }
  // Already an Outcome value (or a subtype of the target): no introduction.
  if (Subtyping(ctx, from, to).subtype) {
    return OutcomeIntro::None;
  }
  const bool to_value = Subtyping(ctx, from, sig->value).subtype;
  const bool to_error = Subtyping(ctx, from, sig->error).subtype;
  if (to_value && to_error) {
    return OutcomeIntro::Ambiguous;
  }
  if (to_value) {
    return OutcomeIntro::Value;
  }
  if (to_error) {
    return OutcomeIntro::Error;
  }
  return OutcomeIntro::None;
}

}  // namespace ultraviolet::analysis
