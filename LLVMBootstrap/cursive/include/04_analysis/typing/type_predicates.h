// =================================================================
// File: 03_analysis/types/type_predicates.h
// Construct: Type Predicates (Bitcopy, Clone, Eq, Ord, Cast)
// Spec Section: 5.2.12
// Spec Rules: BitcopyType, CloneType, EqType, OrdType, CastValid
// =================================================================
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"

namespace cursive::analysis {

// Check if a type satisfies Bitcopy predicate
// §12884-12887: Structural derivation for Records, Enums, Modals
bool BitcopyType(const ScopeContext& ctx, const TypeRef& type);

// Check if a type satisfies Clone predicate
bool CloneType(const ScopeContext& ctx, const TypeRef& type);

// Check if a type implements Drop
bool DropType(const ScopeContext& ctx, const TypeRef& type);

// Check if a type is FFI-safe
bool FfiSafeType(const ScopeContext& ctx, const TypeRef& type);

// Structural diagnostic for why a type is not FFI-safe. Returns std::nullopt
// when the type satisfies FfiSafeType.
std::optional<std::string_view> FfiSafeDiagForType(
    const ScopeContext& ctx,
    const TypeRef& type);

std::optional<std::string_view> FfiSafeDiagForType(
    const ScopeContext& ctx,
    const ast::ModulePath& current_module,
    const TypeRef& type);

// Structural diagnostic for why a type is not GpuSafeType. Returns std::nullopt
// when the type satisfies the currently implemented GPU-safety rules.
std::optional<std::string_view> GpuSafeDiagForType(
    const ScopeContext& ctx,
    const TypeRef& type);

// Check if a type has a unique all-zero object representation.
bool ZeroableType(const ScopeContext& ctx, const TypeRef& type);

// Check if a type supports equality comparison
bool EqType(const TypeRef& type);

// Check if a type has intrinsic built-in Step support.
bool BuiltinStepType(const TypeRef& type);

struct FoundationalBuiltinMethodSig {
  Permission recv_perm = Permission::Const;
  TypeRef recv_type;
  std::vector<TypeFuncParam> params;
  TypeRef ret;
};

// Lookup intrinsic built-in Eq/Step method signatures on types that satisfy
// the corresponding foundational predicates intrinsically.
std::optional<FoundationalBuiltinMethodSig> LookupFoundationalBuiltinMethodSig(
    const TypeRef& recv_base,
    std::string_view name);

// Check if a type supports ordering comparison
bool OrdType(const TypeRef& type);

// Check if a cast from source to target is valid
bool CastValid(const TypeRef& source, const TypeRef& target);

// Strip permission wrapper from a type
TypeRef StripPerm(const TypeRef& type);

}  // namespace cursive::analysis
