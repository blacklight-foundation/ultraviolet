#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "02_source/ast/ast.h"

namespace cursive::analysis {

struct LinearizationResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<ast::ClassPath> order;
};

LinearizationResult LinearizeClass(const ScopeContext& ctx,
                                   const ast::ClassPath& path);

struct ClassMethodTableResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  struct Entry {
    const ast::ClassMethodDecl* method = nullptr;
    ast::ClassPath owner;
  };
  std::vector<Entry> methods;
};

struct ClassFieldTableResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<const ast::ClassFieldDecl*> fields;
};

ClassMethodTableResult ClassMethodTable(const ScopeContext& ctx,
                                        const ast::ClassPath& path);

ClassFieldTableResult ClassFieldTable(const ScopeContext& ctx,
                                      const ast::ClassPath& path);

const ast::ClassMethodDecl* LookupClassMethod(const ScopeContext& ctx,
                                                 const ast::ClassPath& path,
                                                 std::string_view name);

// Debug-only profiling summary for class method lookup work.
void LogClassLookupPerfSummary();

bool ClassDispatchable(const ScopeContext& ctx, const ast::ClassPath& path);

std::optional<std::string_view> ClassDispatchabilityDiagnostic(
    const ScopeContext& ctx,
    const ast::ClassPath& path);

bool ClassSubtypes(const ScopeContext& ctx,
                   const ast::ClassPath& sub,
                   const ast::ClassPath& sup);

bool TypeImplementsClass(const ScopeContext& ctx,
                         const TypeRef& type,
                         const ast::ClassPath& path);

// C0X Extension: Full classes

// Get associated types from a class
std::vector<const ast::AssociatedTypeDecl*> ClassAssociatedTypes(
    const ast::ClassDecl& decl);

// Get abstract states from a modal class
std::vector<const ast::AbstractStateDecl*> ClassAbstractStates(
    const ast::ClassDecl& decl);

// Check if class is a modal class
bool IsModalClass(const ast::ClassDecl& decl);

// Check if method is vtable-eligible
bool VTableEligible(const ast::ClassMethodDecl& method);

// Check if class is dispatchable (for dynamic dispatch)
bool Dispatchable(const ScopeContext& ctx, const ast::ClassDecl& decl);

// Implementation completeness check
struct CompletenessResult {
  bool ok = true;
  std::optional<std::string_view> diag_id;
  std::vector<std::string> missing_methods;
  std::vector<std::string> missing_types;
  std::vector<std::string> missing_states;
};

CompletenessResult CheckImplCompleteness(
    const ScopeContext& ctx,
    const ast::ClassPath& class_path,
    const ast::RecordDecl& impl);

// Orphan rule check
bool CheckOrphanRule(const ScopeContext& ctx,
                     const TypePath& type_path,
                     const ast::ClassPath& class_path,
                     const ast::ModulePath& current_module);

}  // namespace cursive::analysis
