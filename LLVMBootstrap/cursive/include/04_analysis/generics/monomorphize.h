#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

// C0X Extension: Monomorphization for generics
// Compile-time specialization of generic declarations

// Type substitution map: parameter name -> concrete type
using TypeSubst = std::map<std::string, TypeRef>;

// Instantiation key: (declaration path, type arguments)
struct InstantiationKey {
  TypePath decl_path;
  std::vector<TypeRef> args;
  
  bool operator<(const InstantiationKey& other) const;
  bool operator==(const InstantiationKey& other) const;
};

// Instantiation entry in the graph
struct InstantiationEntry {
  InstantiationKey key;
  bool processed = false;
  std::set<InstantiationKey> dependencies;
};

// Monomorphization context
class MonomorphizeContext {
 public:
  MonomorphizeContext() = default;
  
  // Demand an instantiation (adds to worklist if new)
  void Demand(const InstantiationKey& key);
  
  // Check if instantiation exists
  bool HasInstantiation(const InstantiationKey& key) const;
  
  // Get all instantiations
  const std::map<InstantiationKey, InstantiationEntry>& Instantiations() const {
    return instantiations_;
  }
  
  // Process worklist until fixed point
  // Returns false if non-terminating recursion detected
  bool ProcessToFixedPoint();
  
  // Maximum instantiation depth (IDB max: 128)
  static constexpr std::size_t kMaxDepth = 128;
  
 private:
  std::map<InstantiationKey, InstantiationEntry> instantiations_;
  std::vector<InstantiationKey> worklist_;
  std::size_t current_depth_ = 0;
};

// Instantiate a type with type substitution
TypeRef InstantiateType(const TypeRef& type, const TypeSubst& subst);

// Build substitution map from parameters and arguments
TypeSubst BuildSubstitution(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args);

// Build the substitution defined by ModalRefSubst(modal_ref, M).
TypeSubst BuildModalRefSubstitution(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args);

// Check if type arguments satisfy bounds
struct BoundCheckResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::string param_name;  // Parameter that failed
  std::string type_name;   // Type that failed (alias for where_bounds usage)
  std::string bound_name;  // Bound that wasn't satisfied
  core::DiagnosticStream diagnostics;  // Detailed diagnostics
};

BoundCheckResult CheckBoundsSatisfied(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& args);

// Infer type arguments from value arguments.
// Contextual return-type inference is not enabled in Cursive0.
struct InferResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<TypeRef> inferred_args;
};

InferResult InferTypeArguments(
    const std::vector<ast::TypeParam>& params,
    const std::vector<TypeRef>& expected_param_types,
    const std::vector<TypeRef>& actual_arg_types,
    const std::optional<TypeRef>& expected_return);

}  // namespace cursive::analysis
