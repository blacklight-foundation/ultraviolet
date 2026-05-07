// =============================================================================
// MIGRATION MAPPING: globals.cpp
// =============================================================================
//
// SPEC REFERENCE: CursiveSpecification.md
//   - Section 6.0 CG-Item-Static rule (lines 14272-14275)
//   - EmitGlobal judgment (line 14273)
//   - GlobalConst, GlobalZero IR declarations (referenced in ir_model)
//
// SOURCE FILE: cursive-bootstrap/src/04_codegen/globals.cpp
//   - EmitGlobal function for static declarations
//   - Global initialization handling
//
// DEPENDENCIES:
//   - cursive/include/05_codegen/globals/globals.h
//   - cursive/include/05_codegen/ir/ir_model.h (GlobalConst, GlobalZero)
//   - cursive/include/04_analysis/layout/layout.h (SizeOf, AlignOf)
//   - cursive/include/05_codegen/symbols/mangle.h (MangleStatic)
//
// REFACTORING NOTES:
//   1. Static declarations generate global IR:
//      - `let` statics with const init: GlobalConst (immutable data)
//      - `var` statics: GlobalZero (zeroed, runtime init)
//   2. EmitGlobal returns IRDecls for the static
//   3. Symbol mangled per Section 6.3.1
//   4. Linkage determined by visibility
//   5. Initial value encoded as bytes for GlobalConst
//   6. GlobalZero just needs size/alignment
//
// GLOBAL IR TYPES:
//   GlobalConst {
//     symbol: string,
//     bytes: vector<u8>,
//     size: u64,
//     align: u64,
//     linkage: LinkageKind
//   }
//
//   GlobalZero {
//     symbol: string,
//     size: u64,
//     align: u64,
//     linkage: LinkageKind
//   }
// =============================================================================

#include "05_codegen/globals/globals.h"

#include <cstring>
#include <variant>

#include "05_codegen/symbols/mangle.h"
#include "05_codegen/symbols/linkage.h"
#include "04_analysis/layout/layout.h"
#include "00_core/symbols.h"
#include "04_analysis/typing/type_pattern.h"
#include "00_core/assert_spec.h"

namespace cursive::codegen {

// ============================================================================
// Helper functions
// ============================================================================

// Helpers moved to lower_expr.h
static std::optional<std::vector<std::uint8_t>> ConstInit(analysis::TypeRef type,
                                                          const ast::Expr& expr) {
  if (!type) {
    return std::nullopt;
  }
  if (const auto* lit = std::get_if<ast::LiteralExpr>(&expr.node)) {
    SPEC_RULE("ConstInit");
    return ::cursive::analysis::layout::EncodeConst(type, lit->literal);
  }
  return std::nullopt;
}

// ============================================================================
// Section 6.7 Static Name Extraction
// ============================================================================

std::optional<std::string> StaticName(const ast::Binding& binding) {
  SPEC_DEF("StaticName", "Section 6.7");

  if (!binding.pat) {
    return std::nullopt;
  }

  // StaticName(binding) = name if IdentifierPattern(name)
  if (const auto* ident =
          std::get_if<ast::IdentifierPattern>(&binding.pat->node)) {
    return ident->name;
  }

  // StaticName(binding) = name if TypedPattern(name, _)
  if (const auto* typed =
          std::get_if<ast::TypedPattern>(&binding.pat->node)) {
    if (typed->name == "_") {
      return std::nullopt;
    }
    return typed->name;
  }

  // StaticName(binding) = nullopt otherwise
  return std::nullopt;
}

// Helper to recursively extract names from a pattern
static void CollectPatternNames(const ast::Pattern& pat,
                                std::vector<std::string>& names) {
  if (const auto* ident =
          std::get_if<ast::IdentifierPattern>(&pat.node)) {
    names.push_back(ident->name);
    return;
  }

  if (const auto* typed =
          std::get_if<ast::TypedPattern>(&pat.node)) {
    if (typed->name == "_") {
      return;
    }
    names.push_back(typed->name);
    return;
  }

  if (const auto* tuple =
          std::get_if<ast::TuplePattern>(&pat.node)) {
    for (const auto& elem : tuple->elements) {
      if (elem) {
        CollectPatternNames(*elem, names);
      }
    }
    return;
  }

  if (const auto* record =
          std::get_if<ast::RecordPattern>(&pat.node)) {
    for (const auto& field : record->fields) {
      if (field.pattern_opt) {
        CollectPatternNames(*field.pattern_opt, names);
      }
    }
    return;
  }

  // Wildcard and other patterns don't bind names
}

std::vector<std::string> StaticBindList(const ast::Binding& binding) {
  SPEC_DEF("StaticBindList", "Section 6.7");

  std::vector<std::string> names;
  if (binding.pat) {
    CollectPatternNames(*binding.pat, names);
  }
  return names;
}

std::vector<StaticBindRef> StaticBindOrder(const ast::ASTModule& module) {
  SPEC_DEF("StaticBindOrder", "Section 6.7");

  std::vector<StaticBindRef> order;
  for (const auto* item : StaticItems(module)) {
    const auto names = StaticBindList(item->binding);
    for (const auto& name : names) {
      order.emplace_back(module.path, name);
    }
  }
  return order;
}

std::vector<StaticBindRef> GlobalStaticOrder(
    const analysis::Sigma& sigma,
    const std::vector<ast::ModulePath>& init_order) {
  SPEC_DEF("GlobalStaticOrder", "Section 6.7");

  std::vector<StaticBindRef> order;
  for (const auto& module_path : init_order) {
    const ast::ASTModule* module = nullptr;
    for (const auto& candidate : sigma.mods) {
      if (candidate.path == module_path) {
        module = &candidate;
        break;
      }
    }
    if (!module) {
      continue;
    }
    auto module_order = StaticBindOrder(*module);
    order.insert(order.end(),
                 std::make_move_iterator(module_order.begin()),
                 std::make_move_iterator(module_order.end()));
  }
  return order;
}

// StaticBindTypes(binding) = B when BindType(binding) matches pattern
std::vector<std::pair<std::string, analysis::TypeRef>> StaticBindTypes(
    const ast::Binding& binding,
    const ast::ModulePath& module_path,
    LowerCtx& ctx) {

  if (!binding.pat) {
    return {};
  }

  analysis::TypeRef bind_type;
  if (binding.init && ctx.expr_type) {
    bind_type = ctx.expr_type(*binding.init);
  }
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  if (!bind_type && ann_type && ctx.sigma) {
    analysis::ScopeContext scope;
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = module_path;
    if (auto lowered = ::cursive::analysis::layout::LowerTypeForLayout(scope, ann_type)) {
      bind_type = *lowered;
    }
  }
  if (!bind_type) {
    return {};
  }

  analysis::ScopeContext scope;
  if (ctx.sigma) {
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = module_path;
  }
  const auto pattern_check = analysis::TypePatternAgainstType(scope, binding.pat, bind_type);
  if (!pattern_check.ok) {
    return {};
  }
  return pattern_check.bindings;
}


// ============================================================================
// Section 6.7 Symbol Generation
// ============================================================================

std::string StaticSym(const ast::StaticDecl& decl,
                      const ast::ModulePath& module_path,
                      const std::string& name) {
  SPEC_DEF("StaticSym", "Section 6.7");

  // StaticSym(StaticDecl(..., binding,...), x) =
  //   Mangle(StaticDecl(...)) if StaticName(binding) = x
  //   Mangle(StaticBinding(StaticDecl(...), x)) otherwise

  auto static_name = StaticName(decl.binding);
  if (static_name.has_value() && *static_name == name) {
    return MangleStatic(module_path, decl);
  }

  return MangleStaticBinding(module_path, name);
}

std::string StaticSymPath(const ast::ModulePath& path,
                          const std::string& name) {
  SPEC_DEF("StaticSymPath", "Section 6.7");

  // StaticSymPath(path, name) uses MangleStaticBinding
  return MangleStaticBinding(path, name);
}

const ast::StaticDecl* StaticItemOf(const analysis::Sigma& sigma,
                                    const ast::ModulePath& path,
                                    const std::string& name) {
  SPEC_DEF("StaticItemOf", "Section 6.7");

  const ast::ASTModule* module = nullptr;
  for (const auto& candidate : sigma.mods) {
    if (candidate.path == path) {
      module = &candidate;
      break;
    }
  }
  if (!module) {
    return nullptr;
  }

  const ast::StaticDecl* found = nullptr;
  for (const auto* item : StaticItems(*module)) {
    const auto names = StaticBindList(item->binding);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      continue;
    }
    if (found && found != item) {
      return nullptr;
    }
    found = item;
  }
  return found;
}

std::string StaticSymPath(const analysis::Sigma& sigma,
                          const ast::ModulePath& path,
                          const std::string& name) {
  SPEC_DEF("StaticSymPath", "Section 6.7");

  if (const auto* item = StaticItemOf(sigma, path, name)) {
    return StaticSym(*item, path, name);
  }
  return StaticSymPath(path, name);
}

std::optional<IRValue> StaticAddr(const analysis::Sigma& sigma,
                                  const ast::ModulePath& path,
                                  const std::string& name) {
  SPEC_DEF("StaticAddr", "Section 6.7");

  if (const auto* item = StaticItemOf(sigma, path, name)) {
    IRValue addr;
    addr.kind = IRValue::Kind::Symbol;
    addr.name = StaticSym(*item, path, name);
    return addr;
  }
  return std::nullopt;
}

bool IsMoveExprLiteLocal(const ast::ExprPtr& expr) {
  return expr && std::holds_alternative<ast::MoveExpr>(expr->node);
}

bool IsPlaceExprLiteLocal(const ast::ExprPtr& expr) {
  if (!expr) {
    return false;
  }
  return std::visit(
      [&](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierExpr>) {
          return true;
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          return IsPlaceExprLiteLocal(node.base);
        } else if constexpr (std::is_same_v<T, ast::TupleAccessExpr>) {
          return IsPlaceExprLiteLocal(node.base);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          return IsPlaceExprLiteLocal(node.base);
        } else if constexpr (std::is_same_v<T, ast::DerefExpr>) {
          return IsPlaceExprLiteLocal(node.value);
        }
        return false;
      },
      expr->node);
}

bool StaticHasResponsibilityLocal(const ast::StaticDecl& item) {
  const auto& init = item.binding.init;
  if (!init) {
    return true;
  }
  if (!IsPlaceExprLiteLocal(init)) {
    return true;
  }
  return IsMoveExprLiteLocal(init);
}

analysis::TypeRef StaticType(const analysis::Sigma& sigma,
                             const ast::ModulePath& path,
                             const std::string& name) {
  SPEC_DEF("StaticType", "Section 6.7");

  const auto* item = StaticItemOf(sigma, path, name);
  if (!item) {
    return nullptr;
  }

  LowerCtx tmp;
  tmp.sigma = &sigma;
  tmp.module_path = path;
  const auto bind_types = StaticBindTypes(item->binding, path, tmp);
  for (const auto& [bind_name, bind_type] : bind_types) {
    if (bind_name == name) {
      return bind_type;
    }
  }
  return nullptr;
}

std::optional<StaticBindingInfo> StaticBindInfo(const analysis::Sigma& sigma,
                                                const ast::ModulePath& path,
                                                const std::string& name) {
  SPEC_DEF("StaticBindInfo", "Section 6.7");

  const auto* item = StaticItemOf(sigma, path, name);
  if (!item) {
    return std::nullopt;
  }

  StaticBindingInfo info;
  info.type = StaticType(sigma, path, name);
  if (!info.type) {
    return std::nullopt;
  }
  info.has_responsibility = StaticHasResponsibilityLocal(*item);
  info.is_immovable = item->binding.op.lexeme == ":=" || !info.has_responsibility;
  info.mut = item->mut;
  return info;
}

// ============================================================================
// Section 6.7 Global Emission
// ============================================================================

analysis::TypeRef StaticInitTypeForGlobal(const ast::StaticDecl& item,
                                          const ast::ModulePath& module_path,
                                          LowerCtx& ctx) {
  const auto& binding = item.binding;
  analysis::TypeRef init_type;
  if (binding.init && ctx.expr_type) {
    init_type = ctx.expr_type(*binding.init);
  }
  const auto ann_type = ast::BindingAnnotationTypeOpt(binding);
  if (!init_type && ann_type && ctx.sigma) {
    analysis::ScopeContext scope;
    scope.sigma = *ctx.sigma;
    scope.sigma_source = ctx.sigma;
    scope.current_module = module_path;
    if (auto lowered =
            ::cursive::analysis::layout::LowerTypeForLayout(scope, ann_type)) {
      init_type = *lowered;
    }
  }
  return init_type;
}

void RegisterStaticMetadata(const ast::StaticDecl& item,
                            const ast::ModulePath& module_path,
                            LowerCtx& ctx) {
  SPEC_DEF("RegisterStaticMetadata", "Section 6.7");
  const auto& binding = item.binding;
  auto static_name = StaticName(binding);
  if (static_name.has_value()) {
    analysis::TypeRef init_type =
        StaticInitTypeForGlobal(item, module_path, ctx);
    if (init_type) {
      const std::string sym = StaticSym(item, module_path, *static_name);
      ctx.RegisterStaticType(sym, init_type);
      ctx.RegisterStaticModule(sym, module_path);
    }
    return;
  }

  auto bind_types = StaticBindTypes(binding, module_path, ctx);
  for (const auto& [name, type] : bind_types) {
    if (!type) {
      continue;
    }
    const std::string sym = StaticSym(item, module_path, name);
    ctx.RegisterStaticType(sym, type);
    ctx.RegisterStaticModule(sym, module_path);
  }
}

EmitGlobalResult EmitGlobal(const ast::StaticDecl& item,
                            const ast::ModulePath& module_path,
                            LowerCtx& ctx) {
  EmitGlobalResult result;
  result.needs_runtime_init = false;
  const bool externally_visible = LinkageOf(item) == LinkageKind::External;
  const bool export_from_shared_library = item.vis == ast::Visibility::Public;

  const auto& binding = item.binding;
  auto static_name = StaticName(binding);

  analysis::TypeRef init_type =
      StaticInitTypeForGlobal(item, module_path, ctx);

  analysis::ScopeContext layout_scope;
  if (ctx.sigma) {
    layout_scope.sigma = *ctx.sigma;
    layout_scope.sigma_source = ctx.sigma;
    layout_scope.current_module = module_path;
  }

  // Check if this is a simple binding
  if (static_name.has_value()) {
    std::string sym = StaticSym(item, module_path, *static_name);

    RegisterStaticMetadata(item, module_path, ctx);

    if (binding.init) {
      if (item.mut == ast::Mutability::Let) {
        if (auto bytes = ConstInit(init_type, *binding.init)) {
          SPEC_RULE("Emit-Static-Const");
          GlobalConst gc;
          gc.symbol = sym;
          gc.bytes = std::move(*bytes);
          gc.externally_visible = externally_visible;
          gc.export_from_shared_library = export_from_shared_library;
          result.decls.push_back(std::move(gc));
          return result;
        }
      }

      SPEC_RULE("Emit-Static-Init");
      GlobalZero gz;
      gz.symbol = sym;
      gz.externally_visible = externally_visible;
      gz.export_from_shared_library = export_from_shared_library;
      const auto size = ::cursive::analysis::layout::SizeOf(layout_scope, init_type);
      if (!size.has_value()) {
        ctx.ReportCodegenFailure();
        return result;
      }
      gz.size = *size;
      result.decls.push_back(std::move(gz));
      result.needs_runtime_init = true;
      return result;
    }

    SPEC_RULE("Emit-Static-Init");
    GlobalZero gz;
    gz.symbol = sym;
    gz.externally_visible = externally_visible;
    gz.export_from_shared_library = export_from_shared_library;
    const auto size = ::cursive::analysis::layout::SizeOf(layout_scope, init_type);
    if (!size.has_value()) {
      ctx.ReportCodegenFailure();
      return result;
    }
    gz.size = *size;
    result.decls.push_back(std::move(gz));
    return result;
  }

  // (Emit-Static-Multi) - destructuring pattern with multiple bindings
  SPEC_RULE("Emit-Static-Multi");

  auto names = StaticBindList(binding);
  auto bind_types = StaticBindTypes(binding, module_path, ctx);
  RegisterStaticMetadata(item, module_path, ctx);
  for (const auto& name : names) {
    std::string sym = StaticSym(item, module_path, name);
    analysis::TypeRef type;
    for (const auto& [bind_name, bind_type] : bind_types) {
      if (bind_name == name) {
        type = bind_type;
        break;
      }
    }
    GlobalZero gz;
    gz.symbol = sym;
    gz.externally_visible = externally_visible;
    gz.export_from_shared_library = export_from_shared_library;
    const auto size = ::cursive::analysis::layout::SizeOf(layout_scope, type);
    if (!size.has_value()) {
      ctx.ReportCodegenFailure();
      result.decls.clear();
      return result;
    }
    gz.size = *size;
    result.decls.push_back(std::move(gz));
  }

  result.needs_runtime_init = true;
  return result;
}

// ============================================================================
// Section 6.7 Static Store IR Generation
// ============================================================================

IRPtr StaticStoreIR(const ast::StaticDecl& item,
                    const ast::ModulePath& module_path,
                    const std::vector<std::pair<std::string, IRValue>>& binds) {
  SPEC_DEF("StaticStoreIR", "Section 6.7");

  // StaticStoreIR(item, []) = empty
  if (binds.empty()) {
    return EmptyIR();
  }

  // StaticStoreIR(item, [(x, v)] ++ bs) = SeqIR(StoreGlobal(StaticSym(item, x), v), StaticStoreIR(item, bs))
  std::vector<IRPtr> stores;
  stores.reserve(binds.size());

  for (const auto& [name, value] : binds) {
    std::string sym = StaticSym(item, module_path, name);
    IRStoreGlobal store;
    store.symbol = sym;
    store.value = value;
    stores.push_back(MakeIR(std::move(store)));
  }

  return SeqIR(std::move(stores));
}

// ============================================================================
// Section 6.12.14 String/Bytes Literal Emission
// ============================================================================

IRDecl EmitStringLit(const std::string& contents) {
  SPEC_DEF("EmitStringLit", "Section 6.12.14");

  // Generate a unique symbol for this string literal
  std::vector<std::uint8_t> bytes(contents.begin(), contents.end());
  std::string sym = MangleLiteral("string", bytes);

  GlobalConst gc;
  gc.symbol = sym;
  gc.bytes = std::move(bytes);
  return gc;
}

IRDecl EmitBytesLit(const std::vector<std::uint8_t>& contents) {
  SPEC_DEF("EmitBytesLit", "Section 6.12.14");
  SPEC_RULE("EmitLiteral-Bytes");

  // Generate a unique symbol for this bytes literal
  std::string sym = MangleLiteral("bytes", contents);

  GlobalConst gc;
  gc.symbol = sym;
  gc.bytes = contents;
  return gc;
}

// ============================================================================
// Section 6.7 Static Items Query
// ============================================================================

std::vector<const ast::StaticDecl*> StaticItems(
    const ast::ASTModule& module) {
  SPEC_DEF("StaticItems", "Section 6.7");

  std::vector<const ast::StaticDecl*> items;

  for (const auto& item : module.items) {
    if (const auto* static_decl = std::get_if<ast::StaticDecl>(&item)) {
      items.push_back(static_decl);
    }
  }

  return items;
}

// ============================================================================
// Spec Rule Anchors
// ============================================================================

void AnchorGlobalsRules() {
  // Section 6.7 Globals and Initialization
  SPEC_RULE("Emit-Static-Const");
  SPEC_RULE("Emit-Static-Init");
  SPEC_RULE("Emit-Static-Multi");

  // Definitions
  SPEC_DEF("StaticName", "Section 6.7");
  SPEC_DEF("StaticBindList", "Section 6.7");
  SPEC_DEF("StaticBindOrder", "Section 6.7");
  SPEC_DEF("GlobalStaticOrder", "Section 6.7");
  SPEC_DEF("StaticBindTypes", "Section 6.7");
  SPEC_DEF("StaticSym", "Section 6.7");
  SPEC_DEF("StaticSymPath", "Section 6.7");
  SPEC_DEF("StaticItemOf", "Section 6.7");
  SPEC_DEF("StaticAddr", "Section 6.7");
  SPEC_DEF("StaticType", "Section 6.7");
  SPEC_DEF("StaticBindInfo", "Section 6.7");
  SPEC_DEF("StaticStoreIR", "Section 6.7");
  SPEC_DEF("StaticItems", "Section 6.7");
  SPEC_DEF("EmitStringLit", "Section 6.12.14");
  SPEC_DEF("EmitBytesLit", "Section 6.12.14");
}

}  // namespace cursive::codegen
