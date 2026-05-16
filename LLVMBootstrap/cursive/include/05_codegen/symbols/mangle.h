#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "02_source/ast/ast.h"
#include "04_analysis/resolve/scopes.h"
#include "04_analysis/typing/types.h"

namespace cursive::codegen {

// MangleContext provides the scope context needed for symbol mangling.
struct MangleContext {
  const analysis::ScopeContext* scope_ctx = nullptr;
};

// =============================================================================
// Item Path Computation
// =============================================================================

// ItemPath(proc) = PathOfModule(ModuleOf(proc)) ++ [name]
std::vector<std::string> ItemPathProc(const ast::ModulePath& module_path,
                                      const ast::ProcedureDecl& proc);

// ItemPath(method) = RecordPath(R) ++ [method.name]
std::vector<std::string> ItemPathMethod(const analysis::TypePath& record_path,
                                        const ast::MethodDecl& method);

// ItemPath(class_method) = ClassPath(Cl) ++ [method.name]
std::vector<std::string> ItemPathClassMethod(const analysis::TypePath& class_path,
                                             const ast::ClassMethodDecl& method);

// ItemPath(state_method) = ModalPath(M) ++ [S] ++ [method.name]
std::vector<std::string> ItemPathStateMethod(const analysis::TypePath& modal_path,
                                             const std::string& state,
                                             const ast::StateMethodDecl& method);

// ItemPath(transition) = ModalPath(M) ++ [S] ++ [trans.name]
std::vector<std::string> ItemPathTransition(const analysis::TypePath& modal_path,
                                            const std::string& state,
                                            const ast::TransitionDecl& trans);

// ItemPath(static) = PathOfModule(ModuleOf(static)) ++ [StaticName(binding)]
std::vector<std::string> ItemPathStatic(const ast::ModulePath& module_path,
                                        const ast::StaticDecl& decl);

// ItemPath(StaticBinding(decl, x)) = PathOfModule(ModuleOf(decl)) ++ [x]
std::vector<std::string> ItemPathStaticBinding(const ast::ModulePath& module_path,
                                               const std::string& binding_name);

// ItemPath(VTableDecl(T, Cl)) = ["vtable"] ++ PathOfType(T) ++ ["cl"] ++ ClassPath(Cl)
std::vector<std::string> ItemPathVTable(const analysis::TypeRef& type,
                                        const analysis::TypePath& class_path);

// ItemPath(DefaultImpl(T, m)) = ["default"] ++ PathOfType(T) ++ ["cl"] ++ ClassPath(Cl) ++ [m.name]
std::vector<std::string> ItemPathDefaultImpl(const analysis::TypeRef& type,
                                             const analysis::TypePath& class_path,
                                             const std::string& method_name);

// =============================================================================
// PathOfType: Type path encoding for vtable/default impl mangling
// =============================================================================

// Per Section 6.3.1:
//   PathOfType(TypePrim(name)) = ["prim", name]
//   PathOfType(TypeString(st)) = ["string", TypeStateName(st)]
//   PathOfType(TypeBytes(st)) = ["bytes", TypeStateName(st)]
//   PathOfType(TypePath(p)) = p
//   PathOfType(TypeModalState(p, S)) = p ++ [S]
//   PathOfType(T) = undefined otherwise
std::vector<std::string> PathOfType(const analysis::TypeRef& type);

// =============================================================================
// Section 6.3.1 LinkName - FFI attribute-aware symbol resolution
// =============================================================================

// LinkName resolves the link symbol name for a declaration based on attributes:
//   - If [[mangle("name")]] present, return specified name
//   - If [[mangle(none)]] present, return raw identifier
//   - Otherwise, return std::nullopt (caller should use mangled name)
std::optional<std::string> LinkName(const ast::AttributeList& attrs,
                                    const std::string& raw_name);

// HostBodySym(proc) = PathSig([ScopedSym(proc), "__host_body"])
std::string HostBodySym(const ast::ModulePath& module_path,
                        const ast::ProcedureDecl& proc);

// HostThunkLinkName(proc) = LinkName(proc) with the normal scoped fallback.
std::string HostThunkLinkName(const ast::ModulePath& module_path,
                              const ast::ProcedureDecl& proc);

// =============================================================================
// Mangle Functions (Section 6.3.1 Rules)
// =============================================================================

// ScopedSym(item) = PathSig(ItemPath(item))
std::string ScopedSym(const std::vector<std::string>& item_path);

// (Mangle-Closure): PathSig([sym_enc, "_closure" ++ ToString(idx)])
std::string MangleClosure(const std::string& enclosing_sym,
                          std::uint64_t closure_index);

// (Mangle-ClosureEnv): PathSig([sym_closure, "_env"])
std::string MangleClosureEnv(const std::string& closure_sym);

// (Mangle-Proc): Mangle non-main procedure
// (Mangle-Main): Mangle main procedure
// With attribute-aware LinkName resolution
std::string MangleProc(const ast::ModulePath& module_path,
                       const ast::ProcedureDecl& proc);

// Mangle a procedure declaration with module-local overload identity when its
// name is part of an overload set.
std::string MangleProcInModule(const ast::ASTModule& module,
                               const ast::ProcedureDecl& proc);

// (Mangle-Record-Method): Mangle record method
std::string MangleMethod(const analysis::TypePath& record_path,
                         const ast::MethodDecl& method);

// (Mangle-Class-Method): Mangle class method declaration
std::string MangleClassMethod(const analysis::TypePath& class_path,
                              const ast::ClassMethodDecl& method);

// (Mangle-State-Method): Mangle modal state method
std::string MangleStateMethod(const analysis::TypePath& modal_path,
                              const std::string& state,
                              const ast::StateMethodDecl& method);

// (Mangle-Transition): Mangle modal transition
std::string MangleTransition(const analysis::TypePath& modal_path,
                             const std::string& state,
                             const ast::TransitionDecl& trans);

// (Mangle-Static): Mangle static declaration
std::string MangleStatic(const ast::ModulePath& module_path,
                         const ast::StaticDecl& decl);

// (Mangle-StaticBinding): Mangle individual static binding
std::string MangleStaticBinding(const ast::ModulePath& module_path,
                                const std::string& binding_name);

// (Mangle-VTable): Mangle vtable symbol
std::string MangleVTable(const analysis::TypeRef& type,
                         const analysis::TypePath& class_path);

// (Mangle-Literal): Mangle literal data symbol
// Per Section 6.3.1:
//   Mangle(LiteralData(kind, contents)) = PathSig(["cursive", "runtime", "literal", LiteralID(kind, contents)])
std::string MangleLiteral(const std::string& kind,
                          std::span<const std::uint8_t> contents);

// (Mangle-DefaultImpl): Mangle default implementation symbol
std::string MangleDefaultImpl(const analysis::TypeRef& type,
                              const analysis::TypePath& class_path,
                              const std::string& method_name);

// =============================================================================
// Spec Rule Anchors
// =============================================================================

// Emits SPEC_RULE anchors for Section 6.3.1 mangle rules.
void AnchorMangleRules();

}  // namespace cursive::codegen
