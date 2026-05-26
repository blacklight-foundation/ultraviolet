#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

// UVX Extension: Variance computation for generic type parameters

// Variance values
enum class Variance {
  Covariant,      // Output position (+ position)
  Contravariant,  // Input position (- position)
  Invariant,      // Both positions (requires exact match)
  Bivariant,      // Neither position (unused parameter)
};

// Combine two variance values
Variance CombineVariance(Variance outer, Variance inner);

// Invert variance (for contravariant positions)
Variance InvertVariance(Variance v);

// Compute variance of type parameter X in type T
Variance VarianceOf(const TypeRef& type, const std::string& param_name);

// Variance contribution rules

// VarMut(N, X): variance through mutable access (unique/shared)
Variance VarMut(const TypeRef& type, const std::string& param_name);

// VarConst(N, X): variance through immutable access (const)
Variance VarConst(const TypeRef& type, const std::string& param_name);

// Variance context for checking generic subtyping
struct VarianceContext {
  std::map<std::string, Variance> param_variance;
};

// Compute variance map for generic type declaration
VarianceContext ComputeVarianceContext(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& member_types);

// Check generic subtyping with variance
// T1<A1,...,An> <: T2<B1,...,Bn> when forall i:
//   - if param i is covariant: Ai <: Bi
//   - if param i is contravariant: Bi <: Ai
//   - if param i is invariant: Ai == Bi
//   - if param i is bivariant: always true
bool CheckGenericSubtyping(
    const VarianceContext& ctx,
    const std::vector<TypeRef>& args1,
    const std::vector<TypeRef>& args2);

}  // namespace ultraviolet::analysis
