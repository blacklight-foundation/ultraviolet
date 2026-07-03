#pragma once

#include <optional>
#include <string_view>

#include "02_source/ast/ast.h"
#include "04_analysis/typing/types.h"

namespace ultraviolet::analysis {

struct ScopeContext;

struct OutcomeSig {
  TypeRef value;
  TypeRef error;
};

bool IsOutcomeTypePath(const ast::TypePath& path);

TypeRef MakeOutcomeType(TypeRef value_type, TypeRef error_type);

std::optional<OutcomeSig> OutcomeSigOf(const TypeRef& type);

ast::EnumDecl BuildOutcomeEnumDecl();

// Implicit Outcome introduction (§13.1.4 T-Outcome-Intro-Value/Error): whether a
// value of type `from` introduces into the Outcome type `to`.
enum class OutcomeIntro { None, Value, Error, Ambiguous };

// Classifies implicit introduction of `from` into `to`:
//   None      - `to` is not an Outcome, `from` is already (a subtype of) `to`,
//               or `from` is a subtype of neither side;
//   Value     - `from <: T_s` and not `from <: E_s`  (wrap as Outcome::Value);
//   Error     - `from <: E_s` and not `from <: T_s`  (wrap as Outcome::Error);
//   Ambiguous - `from` is admissible to both sides (reject with E-TYP-2261).
OutcomeIntro ClassifyOutcomeIntro(const ScopeContext& ctx,
                                  const TypeRef& from,
                                  const TypeRef& to);

}  // namespace ultraviolet::analysis
