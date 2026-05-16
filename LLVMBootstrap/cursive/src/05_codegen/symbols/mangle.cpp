// =============================================================================
// MIGRATION MAPPING: mangle.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.3.1 Symbol Names and Mangling (lines 15392-15548)
//   - MangleJudg = {Mangle} (line 15396)
//   - Join, PathSig helpers (lines 15401-15404)
//   - ItemPath definitions (lines 15406-15415)
//   - TypeStateName (lines 15417-15418)
//   - PathOfType (lines 15419-15425)
//   - Literal Identity (lines 15427-15434)
//   - LinkName rules (lines 15460-15466)
//   - Mangle-* rules for each item type (lines 15472-15546)
//   - Closure Mangling (lines 15532-15548)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/mangle.cpp
//   - LinkName function (lines 16-65)
//   - TypeStateName helpers (lines 72-90)
//   - StaticName helper (lines 95-113)
//   - ItemPath* functions (lines 121-234)
//   - PathOfType function (lines 240-284)
//   - ScopedSym function (lines 290-297)
//   - Mangle* functions (lines 303-387)
//   - AnchorMangleRules (lines 393-406)
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/symbols/mangle.h (mangle function declarations)
//   - cursive/include/00_core/hash.h (FNV1a64 for literal IDs)
//   - cursive/include/00_core/symbols.h (Mangle, StringOfPath)
//   - cursive/include/02_source/ast/ast.h (declaration types)
//   - cursive/include/04_analysis/typing/types.h (TypeRef)
//
// REFACTORING NOTES:
//   1. ScopedSym(item) = PathSig(ItemPath(item))
//   2. PathSig(p) = mangle(StringOfPath(p))
//   3. ItemPath varies by item type:
//      - Procedures: module_path ++ [name]
//      - Methods: record_path ++ [method_name]
//      - State methods: modal_path ++ [state] ++ [method_name]
//      - VTables: ["vtable"] ++ PathOfType(T) ++ ["cl"] ++ ClassPath
//   4. LinkName checks [[mangle(mode)]] first:
//      - [[mangle(none)]] -> raw identifier
//      - [[mangle("name")]] -> specified symbol
//   5. LiteralID uses FNV1a64 hash of contents
//   6. Closures mangled with enclosing scope + index
//
// MANGLE FUNCTIONS:
//   - MangleProc, MangleMethod, MangleClassMethod
//   - MangleStateMethod, MangleTransition
//   - MangleStatic, MangleStaticBinding
//   - MangleVTable, MangleLiteral, MangleDefaultImpl
// =============================================================================

#include "05_codegen/symbols/mangle.h"

#include <optional>
#include <string_view>
#include <variant>

#include "00_core/assert_spec.h"
#include "02_source/attributes/attribute_registry.h"
#include "00_core/hash.h"
#include "00_core/symbols.h"
#include "01_project/language_profile.h"

#include "02_source/ast/nodes/ast_attributes.h"
#include "02_source/ast/nodes/ast_items.h"
#include "02_source/ast/nodes/ast_patterns.h"
#include "02_source/ast/nodes/ast_stmts.h"

namespace cursive::codegen {

// =============================================================================
// Section 6.3.1 LinkName - FFI attribute-aware symbol resolution
// =============================================================================

std::optional<std::string> LinkName(const ast::AttributeList& attrs,
                                    const std::string& raw_name) {
  SPEC_DEF("LinkName", "6.3.1");

  auto normalize_attr_literal = [](std::string value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
      return value.substr(1, value.size() - 2);
    }
    return value;
  };

  for (const auto& attr : attrs) {
    if (attr.name != analysis::attrs::kMangle) {
      continue;
    }

    // [[mangle(mode)]] with `mode = none | "..."`.
    const ast::AttributeArg* mode_arg = nullptr;
    for (const auto& arg : attr.args) {
      if (!arg.key.has_value() || *arg.key == "mode") {
        mode_arg = &arg;
        break;
      }
    }
    if (!mode_arg) {
      return std::nullopt;
    }

    const auto* tok = std::get_if<ast::Token>(&mode_arg->value);
    if (!tok) {
      return std::nullopt;
    }

    const std::string mode = normalize_attr_literal(tok->lexeme);
    if (mode.empty()) {
      return std::nullopt;
    }
    if (mode == "none" && tok->kind != lexer::TokenKind::StringLiteral) {
      SPEC_RULE("LinkName-NoMangle");
      return raw_name;
    }
    if (tok->kind == lexer::TokenKind::StringLiteral) {
      SPEC_RULE("LinkName-Symbol");
      return mode;
    }
    return std::nullopt;
  }

  // No FFI attribute found - use standard mangling
  return std::nullopt;
}

std::string HostBodySym(const ast::ModulePath& module_path,
                        const ast::ProcedureDecl& proc) {
  const std::string scoped = ScopedSym(ItemPathProc(module_path, proc));
  return ScopedSym({scoped, "__host_body"});
}

std::string HostThunkLinkName(const ast::ModulePath& module_path,
                              const ast::ProcedureDecl& proc) {
  if (auto link_name = LinkName(proc.attrs, proc.name)) {
    return *link_name;
  }
  return ScopedSym(ItemPathProc(module_path, proc));
}

namespace {

// TypeStateName per Section 6.3.1:
//   TypeStateName(View) = "view"
//   TypeStateName(Managed) = "managed"
std::string TypeStateName(analysis::StringState state) {
  switch (state) {
    case analysis::StringState::View:
      return "view";
    case analysis::StringState::Managed:
      return "managed";
  }
  return "";
}

std::string TypeStateName(analysis::BytesState state) {
  switch (state) {
    case analysis::BytesState::View:
      return "view";
    case analysis::BytesState::Managed:
      return "managed";
  }
  return "";
}

// Extract name from a simple binding pattern.
// StaticName(binding) = name if IdentifierPattern(name) or TypedPattern(name, _)
// StaticName(binding) = undefined otherwise
std::optional<std::string> StaticName(const ast::Binding& binding) {
  SPEC_DEF("StaticName", "6.7.2");

  if (!binding.pat) {
    return std::nullopt;
  }

  if (const auto* ident =
          std::get_if<ast::IdentifierPattern>(&binding.pat->node)) {
    return ident->name;
  }

  if (const auto* typed =
          std::get_if<ast::TypedPattern>(&binding.pat->node)) {
    if (typed->name == "_") {
      return std::nullopt;
    }
    return typed->name;
  }

  return std::nullopt;
}

}  // namespace

// =============================================================================
// Item Path Computation
// =============================================================================

std::vector<std::string> ItemPathProc(const ast::ModulePath& module_path,
                                      const ast::ProcedureDecl& proc) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("PathOfModule", "3.4.1");

  std::vector<std::string> path = module_path;
  path.push_back(proc.name);
  return path;
}

std::vector<std::string> ItemPathMethod(const analysis::TypePath& record_path,
                                        const ast::MethodDecl& method) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("RecordPath", "5.3.1");

  std::vector<std::string> path = record_path;
  path.push_back(method.name);
  return path;
}

std::vector<std::string> ItemPathClassMethod(const analysis::TypePath& class_path,
                                             const ast::ClassMethodDecl& method) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("ClassPath", "5.4.1");

  std::vector<std::string> path = class_path;
  path.push_back(method.name);
  return path;
}

std::vector<std::string> ItemPathStateMethod(const analysis::TypePath& modal_path,
                                             const std::string& state,
                                             const ast::StateMethodDecl& method) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("ModalPath", "5.4.2");

  std::vector<std::string> path = modal_path;
  path.push_back(state);
  path.push_back(method.name);
  return path;
}

std::vector<std::string> ItemPathTransition(const analysis::TypePath& modal_path,
                                            const std::string& state,
                                            const ast::TransitionDecl& trans) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("ModalPath", "5.4.2");

  std::vector<std::string> path = modal_path;
  path.push_back(state);
  path.push_back(trans.name);
  return path;
}

std::vector<std::string> ItemPathStatic(const ast::ModulePath& module_path,
                                        const ast::StaticDecl& decl) {
  SPEC_DEF("ItemPath", "6.3.1");

  std::vector<std::string> path = module_path;
  const auto name = StaticName(decl.binding);
  if (name.has_value()) {
    path.push_back(*name);
  }
  return path;
}

std::vector<std::string> ItemPathStaticBinding(const ast::ModulePath& module_path,
                                               const std::string& binding_name) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("StaticBinding", "6.7.2");

  std::vector<std::string> path = module_path;
  path.push_back(binding_name);
  return path;
}

std::vector<std::string> ItemPathVTable(const analysis::TypeRef& type,
                                        const analysis::TypePath& class_path) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("VTableDecl", "6.3.1");

  // ItemPath(VTableDecl(T, Cl)) = ["vtable"] ++ PathOfType(T) ++ ["cl"] ++ ClassPath(Cl)
  std::vector<std::string> path;
  path.push_back("vtable");

  const auto type_path = PathOfType(type);
  path.insert(path.end(), type_path.begin(), type_path.end());

  path.push_back("cl");
  path.insert(path.end(), class_path.begin(), class_path.end());

  return path;
}

std::vector<std::string> ItemPathDefaultImpl(const analysis::TypeRef& type,
                                             const analysis::TypePath& class_path,
                                             const std::string& method_name) {
  SPEC_DEF("ItemPath", "6.3.1");
  SPEC_DEF("DefaultImpl", "6.3.1");

  // ItemPath(DefaultImpl(T, m)) = ["default"] ++ PathOfType(T) ++ ["cl"] ++ ClassPath(Cl) ++ [m.name]
  std::vector<std::string> path;
  path.push_back("default");

  const auto type_path = PathOfType(type);
  path.insert(path.end(), type_path.begin(), type_path.end());

  path.push_back("cl");
  path.insert(path.end(), class_path.begin(), class_path.end());

  path.push_back(method_name);

  return path;
}

// =============================================================================
// PathOfType
// =============================================================================

std::vector<std::string> PathOfType(const analysis::TypeRef& type) {
  SPEC_DEF("PathOfType", "6.3.1");
  SPEC_DEF("TypeStateName", "6.3.1");

  if (!type) {
    return {};
  }

  // PathOfType(TypePrim(name)) = ["prim", name]
  if (const auto* prim = std::get_if<analysis::TypePrim>(&type->node)) {
    return {"prim", prim->name};
  }

  // PathOfType(TypeString(st)) = ["string", TypeStateName(st)]
  if (const auto* str = std::get_if<analysis::TypeString>(&type->node)) {
    if (str->state.has_value()) {
      return {"string", TypeStateName(*str->state)};
    }
    // If state is not specified, use a default representation
    return {"string", "modal"};
  }

  // PathOfType(TypeBytes(st)) = ["bytes", TypeStateName(st)]
  if (const auto* bytes = std::get_if<analysis::TypeBytes>(&type->node)) {
    if (bytes->state.has_value()) {
      return {"bytes", TypeStateName(*bytes->state)};
    }
    return {"bytes", "modal"};
  }

  // PathOfType(TypePath(p)) = p
  if (const auto* path_type = std::get_if<analysis::TypePathType>(&type->node)) {
    return path_type->path;
  }

  // PathOfType(TypeModalState(p, S)) = p ++ [S]
  if (const auto* modal_state = std::get_if<analysis::TypeModalState>(&type->node)) {
    std::vector<std::string> result = modal_state->path;
    result.push_back(modal_state->state);
    return result;
  }

  // PathOfType(T) = undefined otherwise
  return {};
}

// =============================================================================
// ScopedSym
// =============================================================================

std::string ScopedSym(const std::vector<std::string>& item_path) {
  SPEC_DEF("ScopedSym", "6.3.1");
  SPEC_DEF("PathSig", "6.3.1");

  // ScopedSym(item) = PathSig(ItemPath(item))
  // PathSig(p) = mangle(StringOfPath(p))
  return core::Mangle(core::StringOfPath(item_path));
}

std::string MangleClosure(const std::string& enclosing_sym,
                          std::uint64_t closure_index) {
  SPEC_RULE("Mangle-Closure");
  return ScopedSym(
      {enclosing_sym, "_closure" + std::to_string(closure_index)});
}

std::string MangleClosureEnv(const std::string& closure_sym) {
  SPEC_RULE("Mangle-ClosureEnv");
  return ScopedSym({closure_sym, "_env"});
}

// =============================================================================
// Mangle Functions
// =============================================================================

std::string MangleProc(const ast::ModulePath& module_path,
                       const ast::ProcedureDecl& proc) {
  if (analysis::HasAttribute(proc.attrs, analysis::attrs::kHostExport)) {
    SPEC_RULE("Mangle-HostExport-Proc");
    return HostBodySym(module_path, proc);
  }

  // Section 6.3.1 Check FFI attributes first
  if (auto link_name = LinkName(proc.attrs, proc.name)) {
    SPEC_RULE("Mangle-Proc-LinkName");
    return *link_name;
  }

  if (proc.name == "main") {
    SPEC_RULE("Mangle-Main");
  } else {
    SPEC_RULE("Mangle-Proc");
  }

  return ScopedSym(ItemPathProc(module_path, proc));
}

std::string MangleProcInModule(const ast::ASTModule& module,
                               const ast::ProcedureDecl& proc) {
  auto participates_in_overload_set = [](const ast::ProcedureDecl& decl) {
    if (decl.name == "main") {
      return false;
    }
    if (analysis::HasAttribute(decl.attrs, analysis::attrs::kHostExport)) {
      return false;
    }
    return !LinkName(decl.attrs, decl.name).has_value();
  };

  if (!participates_in_overload_set(proc)) {
    return MangleProc(module.path, proc);
  }

  std::size_t overload_count = 0;
  std::size_t overload_index = 0;
  for (const auto& item : module.items) {
    const auto* candidate = std::get_if<ast::ProcedureDecl>(&item);
    if (!candidate || candidate->name != proc.name ||
        !participates_in_overload_set(*candidate)) {
      continue;
    }
    if (candidate == &proc) {
      overload_index = overload_count;
    }
    ++overload_count;
  }

  if (overload_count <= 1) {
    return MangleProc(module.path, proc);
  }

  auto item_path = ItemPathProc(module.path, proc);
  item_path.push_back("$overload");
  item_path.push_back(std::to_string(overload_index));
  return ScopedSym(item_path);
}

std::string MangleMethod(const analysis::TypePath& record_path,
                         const ast::MethodDecl& method) {
  SPEC_RULE("Mangle-Record-Method");

  return ScopedSym(ItemPathMethod(record_path, method));
}

std::string MangleClassMethod(const analysis::TypePath& class_path,
                              const ast::ClassMethodDecl& method) {
  SPEC_RULE("Mangle-Class-Method");

  return ScopedSym(ItemPathClassMethod(class_path, method));
}

std::string MangleStateMethod(const analysis::TypePath& modal_path,
                              const std::string& state,
                              const ast::StateMethodDecl& method) {
  SPEC_RULE("Mangle-State-Method");

  return ScopedSym(ItemPathStateMethod(modal_path, state, method));
}

std::string MangleTransition(const analysis::TypePath& modal_path,
                             const std::string& state,
                             const ast::TransitionDecl& trans) {
  SPEC_RULE("Mangle-Transition");

  return ScopedSym(ItemPathTransition(modal_path, state, trans));
}

std::string MangleStatic(const ast::ModulePath& module_path,
                         const ast::StaticDecl& decl) {
  SPEC_RULE("Mangle-Static");

  return ScopedSym(ItemPathStatic(module_path, decl));
}

std::string MangleStaticBinding(const ast::ModulePath& module_path,
                                const std::string& binding_name) {
  SPEC_RULE("Mangle-StaticBinding");

  return ScopedSym(ItemPathStaticBinding(module_path, binding_name));
}

std::string MangleVTable(const analysis::TypeRef& type,
                         const analysis::TypePath& class_path) {
  SPEC_RULE("Mangle-VTable");

  return ScopedSym(ItemPathVTable(type, class_path));
}

std::string MangleLiteral(const std::string& kind,
                          std::span<const std::uint8_t> contents) {
  SPEC_RULE("Mangle-Literal");
  SPEC_DEF("LiteralData", "6.3.1");

  // Mangle(LiteralData(kind, contents)) = PathSig(["cursive", "runtime", "literal", LiteralID(kind, contents)])
  const std::string literal_id = core::LiteralID(kind, contents);
  return project::RuntimePathSig({"literal", literal_id});
}

std::string MangleDefaultImpl(const analysis::TypeRef& type,
                              const analysis::TypePath& class_path,
                              const std::string& method_name) {
  SPEC_RULE("Mangle-DefaultImpl");

  return ScopedSym(ItemPathDefaultImpl(type, class_path, method_name));
}

// =============================================================================
// Spec Rule Anchors
// =============================================================================

void AnchorMangleRules() {
  // Section 6.3.1 Mangle Rules
  SPEC_RULE("Mangle-Proc");
  SPEC_RULE("Mangle-Main");
  SPEC_RULE("Mangle-Record-Method");
  SPEC_RULE("Mangle-Class-Method");
  SPEC_RULE("Mangle-State-Method");
  SPEC_RULE("Mangle-Transition");
  SPEC_RULE("Mangle-Static");
  SPEC_RULE("Mangle-StaticBinding");
  SPEC_RULE("Mangle-VTable");
  SPEC_RULE("Mangle-Literal");
  SPEC_RULE("Mangle-DefaultImpl");
  SPEC_RULE("Mangle-Closure");
  SPEC_RULE("Mangle-ClosureEnv");
}

}  // namespace cursive::codegen
