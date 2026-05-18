#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "00_core/diagnostics.h"
#include "04_analysis/composite/record_methods.h"
#include "04_analysis/resolve/resolve_items.h"
#include "04_analysis/typing/context.h"
#include "04_analysis/typing/types.h"
#include "04_analysis/typing/type_lower.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::analysis {

// =============================================================================
// COMMON RESULT TYPES
// =============================================================================

struct DeclTypingResult {
  bool ok = false;
  core::DiagnosticStream diags;
};

// =============================================================================
// SIGNATURE RESULT TYPES (signature.cpp)
// =============================================================================

// RecvTypeResult is defined in 04_analysis/composite/record_methods.h

struct SignatureResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<std::pair<std::string, TypeRef>> bindings;
  TypeRef func_type;
  TypeRef return_type;
};

// =============================================================================
// IMPORT RESULT TYPES (import_decl.cpp)
// =============================================================================

struct ImportDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::ModulePath path;
  std::string alias;
  bool resolved = false;
};

struct ImportResolveResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  ast::ModulePath path;
  bool resolved = false;
  std::vector<std::string> exported_names;
};

// =============================================================================
// RECORD RESULT TYPES (record_decl.cpp)
// =============================================================================

struct FieldInfo {
  std::string name;
  TypeRef type;
};

struct MethodInfo {
  std::string name;
  TypeRef type;
};

struct RecordDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef self_type;
  std::vector<FieldInfo> fields;
  std::vector<MethodInfo> methods;
  bool default_constructible = false;
  std::string diag_detail;
};

// =============================================================================
// ENUM RESULT TYPES (enum_decl.cpp)
// =============================================================================

struct VariantInfo {
  std::string name;
  std::uint64_t discriminant = 0;
  std::vector<TypeRef> payload;
};

struct EnumDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef self_type;
  std::vector<VariantInfo> variants;
  std::vector<MethodInfo> methods;
};

// =============================================================================
// CLASS RESULT TYPES (class_decl.cpp)
// =============================================================================

struct ClassMethodInfo {
  std::string name;
  TypeRef func_type;
  bool has_default = false;
};

struct ClassAssocTypeInfo {
  std::string name;
  TypeRef default_type;
};

struct ClassFieldInfo {
  std::string name;
  TypeRef type;
};

struct ClassDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string name;
  TypePath class_path;
  TypeRef self_type;
  std::vector<std::string> generic_params;
  std::vector<ast::ClassPath> superclasses;
  std::vector<ClassMethodInfo> methods;
  std::vector<ClassAssocTypeInfo> assoc_types;
  std::vector<ClassFieldInfo> fields;
  std::vector<std::string> abstract_states;
  bool is_modal = false;
  std::string diag_detail;
};

// =============================================================================
// MODAL RESULT TYPES (modal_decl.cpp)
// =============================================================================

struct StateFieldInfo {
  std::string name;
  TypeRef type;
};

struct StateMethodInfo {
  std::string name;
  TypeRef func_type;
};

struct TransitionInfo {
  std::string name;
  TypeRef func_type;
  std::string target_state;
};

struct StateInfo {
  std::string name;
  std::vector<StateFieldInfo> fields;
  std::vector<StateMethodInfo> methods;
  std::vector<TransitionInfo> transitions;
};

struct ModalDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  TypeRef self_type;
  std::vector<std::string> generic_params;
  std::vector<StateInfo> states;
  std::string initial_state;
  std::string diag_detail;
};

// =============================================================================
// STATIC RESULT TYPES (static_decl.cpp)
// =============================================================================

struct StaticDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string name;
  TypeRef type;
  bool is_mutable = false;
};

// =============================================================================
// PROCEDURE RESULT TYPES (procedure_decl.cpp)
// =============================================================================

struct ProcedureDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string name;
  TypeRef func_type;
  std::vector<std::string> generic_params;
  std::string diag_detail;
  std::optional<core::Span> diag_span;
};

// =============================================================================
// VISIBILITY RESULT TYPES (visibility.cpp)
// =============================================================================

struct VisibilityCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

// =============================================================================
// TYPE ALIAS RESULT TYPES (type_alias_decl.cpp)
// =============================================================================

struct TypeAliasDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<std::string> generic_params;
  TypeRef aliased_type;
  bool has_refinement = false;
};

// =============================================================================
// GENERIC PARAMS RESULT TYPES (generic_params.cpp)
// =============================================================================

struct GenericParamInfo {
  std::string name;
  std::vector<ast::TypeBound> class_bounds;
  TypeRef default_type;
};

struct GenericParamsResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<GenericParamInfo> params;
};

struct GenericArgsCheckResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

// =============================================================================
// WHERE CLAUSE RESULT TYPES
// =============================================================================

struct WhereClauseResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

DeclTypingResult DeclTypingModules(ScopeContext& ctx,
                                   const std::vector<ast::ASTModule>& modules,
                                   const NameMapTable& name_maps);

DeclTypingResult MainCheckProject(ScopeContext& ctx,
                                  const std::vector<ast::ASTModule>& modules);

// Signature functions
std::optional<ParamMode> LowerParamMode(const std::optional<ast::ParamMode>& mode);

RecvTypeResult RecvTypeForReceiver(
    const ScopeContext& ctx,
    const TypeRef& self_type,
    const ast::Receiver& receiver,
    const std::function<LowerTypeResult(const std::shared_ptr<ast::Type>&)>& lower_type);

TypeRef SubstSelfType(const TypeRef& self_type,
                      const TypeRef& type);
TypeRef SubstSelfType(const TypeRef& self_type,
                      const TypeRef& type,
                      const TypeSubst* assoc_subst);

SignatureResult BuildProcedureSignature(
    const ScopeContext& ctx,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Type>& return_type_opt);

SignatureResult BuildMethodSignature(
    const ScopeContext& ctx,
    const TypeRef& self_type,
    const ast::Receiver& receiver,
    const std::vector<ast::Param>& params,
    const std::shared_ptr<ast::Type>& return_type_opt,
    const TypeSubst* assoc_subst = nullptr);

SignatureResult BuildTransitionSignature(
    const ScopeContext& ctx,
    const TypeRef& source_self_type,
    const TypeRef& target_self_type,
    const std::vector<ast::Param>& params,
    const TypeSubst* assoc_subst = nullptr);

// Import functions
ImportDeclResult TypeImportDecl(
    const ScopeContext& ctx,
    const ast::ImportDecl& decl,
    const ast::ModulePath& current_module);

ImportResolveResult ResolveImportPath(
    const ScopeContext& ctx,
    const ast::ModulePath& path);

bool CheckImportCycle(
    const ScopeContext& ctx,
    const ast::ModulePath& importing_module,
    const ast::ModulePath& imported_module);

// Record functions
RecordDeclResult TypeRecordDecl(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

RecordDeclResult TypeRecordDeclSignature(
    const ScopeContext& ctx,
    const ast::RecordDecl& decl,
    const ast::ModulePath& module_path);

// Enum functions
EnumDeclResult TypeEnumDecl(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

EnumDeclResult TypeEnumDeclSignature(
    const ScopeContext& ctx,
    const ast::EnumDecl& decl,
    const ast::ModulePath& module_path);

// Class functions
ClassDeclResult TypeClassDecl(
    const ScopeContext& ctx,
    const ast::ClassDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

ClassDeclResult TypeClassDeclSignature(
    const ScopeContext& ctx,
    const ast::ClassDecl& decl,
    const ast::ModulePath& module_path);

// Modal functions
ModalDeclResult TypeModalDecl(
    const ScopeContext& ctx,
    const ast::ModalDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

ModalDeclResult TypeModalDeclSignature(
    const ScopeContext& ctx,
    const ast::ModalDecl& decl,
    const ast::ModulePath& module_path);

// Static functions
StaticDeclResult TypeStaticDecl(
    const ScopeContext& ctx,
    const ast::StaticDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

StaticDeclResult TypeStaticDeclSignature(
    const ScopeContext& ctx,
    const ast::StaticDecl& decl,
    const ast::ModulePath& module_path);

// Procedure functions
ProcedureDeclResult TypeProcedureDecl(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

ProcedureDeclResult TypeProcedureDeclSignature(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl);

ProcedureDeclResult TypeProcedureDeclBody(
    const ScopeContext& ctx,
    const ast::ProcedureDecl& decl,
    const ast::ModulePath& module_path,
    const TypeRef& return_type,
    core::DiagnosticStream& diags);

// Debug-only profiling summary for procedure declaration typing.
void LogProcedureTypePerfSummary();

// Visibility functions
int VisRank(ast::Visibility vis);

bool IsVisible(ast::Visibility item_vis,
               const ast::ModulePath& item_module,
               const ast::ModulePath& from_module,
               const std::string& assembly_name,
               const std::string& from_assembly);

VisibilityCheckResult CheckFieldVisibility(
    ast::Visibility field_vis,
    ast::Visibility type_vis);

VisibilityCheckResult CheckVisibilityCoercion(
    ast::Visibility exposed_vis,
    ast::Visibility type_vis);

ast::Visibility DefaultVisibility();

std::string_view VisibilityToString(ast::Visibility vis);

// Type alias functions
TypeAliasDeclResult TypeTypeAliasDecl(
    const ScopeContext& ctx,
    const ast::TypeAliasDecl& decl,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

TypeAliasDeclResult TypeTypeAliasDeclSignature(
    const ScopeContext& ctx,
    const ast::TypeAliasDecl& decl,
    const ast::ModulePath& module_path);

// Where clause processing
WhereClauseResult ProcessWhereClause(
    const ScopeContext& ctx,
    const std::vector<ast::PredicateReq>& predicates,
    const std::vector<std::string>& type_param_names);

// =============================================================================
// USING DECLARATION RESULT TYPES (using_decl.cpp)
// =============================================================================

struct UsingItemResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string item_name;
  std::string alias;
  ast::Path full_path;
  bool resolved = false;
  bool is_type = false;
};

struct UsingDeclResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<UsingItemResult> items;
  bool is_glob = false;
};

struct UsingConflictResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string conflicting_alias;
  ast::Path existing_path;
  ast::Path new_path;
};

struct ScopeBinding {
  std::string name;
  ast::Path target_path;
  bool is_type = false;
  bool from_using = false;
};

struct InjectUsingResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::vector<std::string> injected_names;
  std::vector<std::string> shadowed_by_local;
};

// Using declaration functions
UsingDeclResult TypeUsingDecl(
    const ScopeContext& ctx,
    const ast::UsingDecl& decl,
    const ast::ModulePath& current_module);

UsingConflictResult CheckUsingConflict(
    const ScopeContext& ctx,
    const std::vector<UsingItemResult>& existing_items,
    const UsingItemResult& new_item);

InjectUsingResult InjectUsingItems(
    ScopeContext& ctx,
    const UsingDeclResult& using_result,
    const ast::ModulePath& current_module);

bool IsItemVisible(const ScopeContext& ctx,
                   const ast::Path& item_path,
                   const ast::ModulePath& from_module);

bool IsModuleVisible(const ScopeContext& ctx,
                     const ast::ModulePath& module_path,
                     const ast::ModulePath& from_module);

// =============================================================================
// EXTERN BLOCK RESULT TYPES (extern_block.cpp)
// =============================================================================

enum class ForeignVerificationMode {
  Static,
  Dynamic
};

struct ExternProcInfo {
  std::string name;
  std::vector<TypeRef> param_types;
  TypeRef return_type;
  TypeRef func_type;
  ForeignVerificationMode verification_mode = ForeignVerificationMode::Static;
};

struct ExternBlockResult {
  bool ok = false;
  std::optional<std::string_view> diag_id;
  std::string abi;
  std::vector<ExternProcInfo> procedures;
};

// Extern block functions
ExternBlockResult TypeExternBlock(
    const ScopeContext& ctx,
    const ast::ExternBlock& block,
    const ast::ModulePath& module_path,
    core::DiagnosticStream& diags);

ExternBlockResult TypeExternBlockSignature(
    const ScopeContext& ctx,
    const ast::ExternBlock& block,
    const ast::ModulePath& module_path);

// =============================================================================
// GENERIC PARAMS FUNCTIONS
// =============================================================================

GenericParamsResult ProcessGenericParams(
    const ScopeContext& ctx,
    const std::vector<ast::TypeParam>& params);

GenericParamsResult ProcessGenericParams(
    const ScopeContext& ctx,
    const std::optional<ast::GenericParams>& params_opt);

GenericArgsCheckResult CheckGenericArgs(
    const ScopeContext& ctx,
    const std::vector<GenericParamInfo>& params,
    const std::vector<TypeRef>& args);

TypeRef ApplyGenericSubstitution(
    const TypeRef& type,
    const std::vector<std::string>& param_names,
    const std::vector<TypeRef>& args);

bool TypeImplementsClass(const ScopeContext& ctx,
                         const TypeRef& type,
                         const ast::ClassPath& class_path);

}  // namespace ultraviolet::analysis
