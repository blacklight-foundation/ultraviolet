#pragma once

// UVX Extension: Concurrency & Async Built-in Types (§18, §19)
//
// This header declares the built-in types for structured concurrency and async:
// - ExecutionDomain class (§18.2.4)
// - Spawned<T> modal type (§18.4.2)
// - CancelToken modal type (§18.6.1)
// - Tracked<T, E> modal type (§5.4.4)
// - Async<TOut, TIn, TResult, TError> modal type and aliases (§5.4.5)

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

// -----------------------------------------------------------------------------
// ExecutionDomain class (§18.2.4)
// -----------------------------------------------------------------------------

// Method signature for ExecutionDomain methods
struct ExecutionDomainMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

// Check if a class path refers to the ExecutionDomain class
bool IsExecutionDomainClassPath(const ast::ClassPath& path);

// Look up a method signature on ExecutionDomain
std::optional<ExecutionDomainMethodSig> LookupExecutionDomainMethodSig(
    std::string_view name);

// Check if a type path is the ExecutionDomain dynamic type
bool IsExecutionDomainTypePath(const ast::TypePath& path);
bool IsCpuDomainTypePath(const ast::TypePath& path);
bool IsGpuDomainTypePath(const ast::TypePath& path);
bool IsInlineDomainTypePath(const ast::TypePath& path);

// GPU intrinsic procedure names and synthesized function signatures
bool IsGpuIntrinsicName(std::string_view name);
bool IsGpuExecutionBarrierName(std::string_view name);
std::optional<TypeRef> LookupGpuIntrinsicType(std::string_view name);

// Built-in execution domain class declarations
ast::ClassDecl BuildCpuDomainClassDecl();
ast::ClassDecl BuildGpuDomainClassDecl();
ast::ClassDecl BuildInlineDomainClassDecl();

// -----------------------------------------------------------------------------
// Spawned<T> modal type (§18.4.2)
// -----------------------------------------------------------------------------

// Check if a type path refers to Spawned
bool IsSpawnedTypePath(const ast::TypePath& path);

// Valid states for Spawned<T>
// @Pending - task has been created but not completed
// @Ready   - task has completed, value is available
bool IsValidSpawnedState(std::string_view state);

// Create a Spawned<T> type with the given inner type
TypeRef MakeSpawnedType(const TypeRef& inner_type);

// Create a Spawned<T>@State type
TypeRef MakeSpawnedTypeWithState(const TypeRef& inner_type,
                                 std::string_view state);

// Extract the inner type T from Spawned<T>
std::optional<TypeRef> ExtractSpawnedInner(const TypeRef& type);

// ---------------------------------------------------------------------------
// Tracked<T, E> modal type (§5.4.4)
// ---------------------------------------------------------------------------

bool IsTrackedTypePath(const ast::TypePath& path);

bool IsValidTrackedState(std::string_view state);

TypeRef MakeTrackedType(const TypeRef& value_type, const TypeRef& err_type);

TypeRef MakeTrackedTypeWithState(const TypeRef& value_type,
                                 const TypeRef& err_type,
                                 std::string_view state);

std::optional<std::pair<TypeRef, TypeRef>> ExtractTrackedArgs(
    const TypeRef& type);

// -----------------------------------------------------------------------------
// CancelToken modal type (§18.6.1)
// -----------------------------------------------------------------------------

// Check if a type path refers to CancelToken
bool IsCancelTokenTypePath(const ast::TypePath& path);

// Valid states for CancelToken
// @Active    - cancellation has not been requested
// @Cancelled - cancellation has been requested
bool IsValidCancelTokenState(std::string_view state);

// Method signature for CancelToken methods
struct CancelTokenMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
  std::string_view valid_states;  // Comma-separated states where method is valid
};

// Look up a method signature on CancelToken
std::optional<CancelTokenMethodSig> LookupCancelTokenMethodSig(
    std::string_view name,
    std::string_view state);

// Create a CancelToken type (no state)
TypeRef MakeCancelTokenType();

// Create a CancelToken@State type
TypeRef MakeCancelTokenTypeWithState(std::string_view state);

// -----------------------------------------------------------------------------
// Built-in type declarations for sigma population
// -----------------------------------------------------------------------------

// Build Spawned modal declaration (states: Pending, Ready)
// Note: Spawned<T> is generic; this builds a non-generic version for name lookup
ast::ModalDecl BuildSpawnedModalDecl();

// Build CancelToken modal declaration (states: Active, Cancelled)
ast::ModalDecl BuildCancelTokenModalDecl();

// Build Tracked modal declaration (states: Pending, Ready)
ast::ModalDecl BuildTrackedModalDecl();

// Build Async modal declaration (states: Suspended, Completed, Failed)
ast::ModalDecl BuildAsyncModalDecl();
// Build Async class declaration surface (Appendix B async_class grammar form)
ast::ClassDecl BuildAsyncClassDecl();

// Built-in async aliases
ast::TypeAliasDecl BuildSequenceAliasDecl();
ast::TypeAliasDecl BuildFutureAliasDecl();
ast::TypeAliasDecl BuildStreamAliasDecl();
ast::TypeAliasDecl BuildPipeAliasDecl();
ast::TypeAliasDecl BuildExchangeAliasDecl();

// Build ExecutionDomain class declaration
ast::ClassDecl BuildExecutionDomainClassDecl();

// Reactor capability class (§5.9.2, §19.4)
bool IsReactorClassPath(const ast::ClassPath& path);
ast::ClassDecl BuildReactorClassDecl();

}  // namespace ultraviolet::analysis
