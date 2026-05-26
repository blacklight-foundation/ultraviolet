#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

struct ScopeContext;

// =============================================================================
// System interface (§5.9.4)
// =============================================================================

struct SystemMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

std::optional<SystemMethodSig> LookupSystemMethodSig(std::string_view name);

// =============================================================================
// Context interface (§5.9.4)
// =============================================================================

// UVX Extension: Context method signatures for execution domains (§18.2)
struct ContextMethodSig {
  Permission recv_perm;
  std::vector<ast::Param> params;
  TypeRef ret;
};

std::optional<ContextMethodSig> LookupContextMethodSig(
    std::string_view name,
    std::optional<std::size_t> arg_count = std::nullopt);

// Context field type lookup
std::optional<TypeRef> ContextFieldType(std::string_view field_name);
std::optional<TypeRef> ContextBundleFieldType(std::string_view field_name);

// Type path predicates
bool IsContextTypePath(const ast::TypePath& path);
bool IsSystemTypePath(const ast::TypePath& path);
bool IsRegionOptionsTypePath(const ast::TypePath& path);
std::optional<ast::Path> LookupBuiltinRecordCtorPath(const ast::TypePath& path);
std::optional<ast::Path> LookupBuiltinRecordCtorPath(std::string_view ident);

// Built-in record declarations
ast::RecordDecl BuildContextRecordDecl();
ast::RecordDecl BuildTestAuthorityRecordDecl();
ast::RecordDecl BuildPanicRecordDecl();
ast::RecordDecl BuildRegionOptionsRecordDecl();
ast::RecordDecl BuildSystemRecordDecl();

// =============================================================================
// Main signature validation (§5.9.4)
// =============================================================================

struct MainSignatureResult {
  bool valid = false;
  std::string error_code;
  std::string error_message;
};

bool IsContextBundleType(const ScopeContext& ctx, const ast::Type& type);
bool IsHostedContextBundleType(const ScopeContext& ctx, const ast::Type& type);

/// Validate that a procedure has the correct main signature:
///   public procedure main(ctx: ContextBundle) -> i32
MainSignatureResult ValidateMainSignature(const ScopeContext& ctx,
                                          const ast::ProcedureDecl& proc);

// =============================================================================
// Built-in concurrency types (§5.9.4)
// =============================================================================

ast::TypeAliasDecl BuildCpuSetAliasDecl();
ast::EnumDecl BuildPriorityEnumDecl();

// =============================================================================
// Capability type predicates
// =============================================================================

/// Check if a type is a capability type (dynamic class or Context)
bool IsCapabilityType(const TypeRef& type);

/// Check if a class path refers to a capability class
bool IsCapabilityClassPath(const ast::ClassPath& path);

/// Check if a type contains any capability types (for FFI isolation)
bool TypeContainsCapability(const TypeRef& type);

}  // namespace ultraviolet::analysis
