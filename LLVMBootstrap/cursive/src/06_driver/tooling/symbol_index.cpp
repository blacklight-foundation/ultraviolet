#include "06_driver/tooling/symbol_index.h"

#include <algorithm>
#include <type_traits>

#include "00_core/symbols.h"
#include "06_driver/tooling/uri.h"

namespace cursive::driver::tooling {

namespace {

std::string DocText(const ast::DocList& docs) {
  std::string out;
  for (const auto& doc : docs) {
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += doc.text;
  }
  return out;
}

std::string DocText(const std::optional<ast::DocList>& docs) {
  return docs.has_value() ? DocText(*docs) : std::string();
}

bool Contains(const core::Span& span,
              const std::filesystem::path& path,
              std::size_t byte_offset) {
  return PathKey(span.file) == PathKey(path) && span.start_offset <= byte_offset &&
         byte_offset <= span.end_offset;
}

std::string Qualified(std::string_view module_path, std::string_view name) {
  std::string out(module_path);
  if (!out.empty()) {
    out += "::";
  }
  out += name;
  return out;
}

void AddSymbol(SymbolIndex& index,
               const ast::ModulePath& module,
               std::string name,
               SymbolKind kind,
               const core::Span& span,
               std::string detail,
               std::string documentation) {
  const std::string module_path = core::StringOfPath(module);
  SymbolInfo symbol;
  symbol.name = std::move(name);
  symbol.qualified_name = Qualified(module_path, symbol.name);
  symbol.module_path = module_path;
  symbol.kind = kind;
  symbol.range = span;
  symbol.selection_range = span;
  symbol.detail = std::move(detail);
  symbol.documentation = std::move(documentation);
  index.Add(std::move(symbol));
}

void IndexPattern(SymbolIndex& index,
                  const ast::ModulePath& module,
                  const ast::PatternPtr& pattern) {
  if (!pattern) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          AddSymbol(index, module, node.name, SymbolKind::Variable,
                    pattern->span, "local binding", {});
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          AddSymbol(index, module, node.name, SymbolKind::Variable,
                    pattern->span, "local binding", {});
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            IndexPattern(index, module, elem);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            if (field.pattern_opt) {
              IndexPattern(index, module, field.pattern_opt);
            } else {
              AddSymbol(index, module, field.name, SymbolKind::Variable,
                        field.span, "local binding", {});
            }
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (const auto& elem : payload.elements) {
                      IndexPattern(index, module, elem);
                    }
                  } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                    for (const auto& field : payload.fields) {
                      if (field.pattern_opt) {
                        IndexPattern(index, module, field.pattern_opt);
                      }
                    }
                  }
                },
                *node.payload_opt);
          }
        }
      },
      pattern->node);
}

std::optional<std::string> FirstPatternName(const ast::PatternPtr& pattern) {
  if (!pattern) {
    return std::nullopt;
  }
  return std::visit(
      [](const auto& node) -> std::optional<std::string> {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          return node.name;
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          return node.name;
        } else {
          return std::nullopt;
        }
      },
      pattern->node);
}

void IndexBlock(SymbolIndex& index,
                const ast::ModulePath& module,
                const ast::BlockPtr& block);

void IndexExpr(SymbolIndex& index,
               const ast::ModulePath& module,
               const ast::ExprPtr& expr) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                      std::is_same_v<T, ast::UnsafeBlockExpr>) {
          IndexBlock(index, module, node.block);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          IndexExpr(index, module, node.cond);
          IndexExpr(index, module, node.then_expr);
          IndexExpr(index, module, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          IndexExpr(index, module, node.scrutinee);
          IndexPattern(index, module, node.pattern);
          IndexExpr(index, module, node.then_expr);
          IndexExpr(index, module, node.else_expr);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          IndexPattern(index, module, node.pattern);
          IndexExpr(index, module, node.iter);
          IndexBlock(index, module, node.body);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (const auto& param : node.params) {
            AddSymbol(index, module, param.name, SymbolKind::Variable,
                      expr->span, "closure parameter", {});
          }
          IndexExpr(index, module, node.body);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr>) {
          IndexExpr(index, module, node.base);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
          IndexExpr(index, module, node.callee);
          for (const auto& arg : node.args) {
            IndexExpr(index, module, arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          IndexExpr(index, module, node.receiver);
          for (const auto& arg : node.args) {
            IndexExpr(index, module, arg.value);
          }
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          IndexExpr(index, module, node.lhs);
          IndexExpr(index, module, node.rhs);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
          IndexExpr(index, module, node.value);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            IndexExpr(index, module, elem);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node,
                                       [&](const ast::ExprPtr& subexpr) {
                                         IndexExpr(index, module, subexpr);
                                       });
        }
      },
      expr->node);
}

void IndexStmt(SymbolIndex& index,
               const ast::ModulePath& module,
               const ast::Stmt& stmt) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          IndexPattern(index, module, node.binding.pat);
          IndexExpr(index, module, node.binding.init);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          IndexExpr(index, module, node.value);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          IndexExpr(index, module, node.value_opt);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
          IndexExpr(index, module, node.place);
          IndexExpr(index, module, node.value);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
          IndexExpr(index, module, node.place);
          IndexExpr(index, module, node.value);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt>) {
          IndexBlock(index, module, node.body);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.alias_opt.has_value()) {
            AddSymbol(index, module, *node.alias_opt, SymbolKind::Variable,
                      node.span, "region alias", {});
          }
          IndexExpr(index, module, node.opts_opt);
          IndexBlock(index, module, node.body);
        } else if constexpr (std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          IndexBlock(index, module, node.body);
        }
      },
      stmt);
}

void IndexBlock(SymbolIndex& index,
                const ast::ModulePath& module,
                const ast::BlockPtr& block) {
  if (!block) {
    return;
  }
  for (const auto& stmt : block->stmts) {
    IndexStmt(index, module, stmt);
  }
  IndexExpr(index, module, block->tail_opt);
}

}  // namespace

void SymbolIndex::Add(SymbolInfo symbol) {
  symbols_.push_back(std::move(symbol));
}

std::vector<const SymbolInfo*> SymbolIndex::SymbolsInFile(
    const std::filesystem::path& path) const {
  const std::string key = PathKey(path);
  std::vector<const SymbolInfo*> out;
  for (const auto& symbol : symbols_) {
    if (PathKey(symbol.range.file) == key) {
      out.push_back(&symbol);
    }
  }
  std::sort(out.begin(), out.end(), [](const SymbolInfo* lhs,
                                       const SymbolInfo* rhs) {
    return lhs->range.start_offset < rhs->range.start_offset;
  });
  return out;
}

const SymbolInfo* SymbolIndex::SymbolAt(const std::filesystem::path& path,
                                        std::size_t byte_offset) const {
  const SymbolInfo* best = nullptr;
  for (const auto& symbol : symbols_) {
    if (!Contains(symbol.selection_range, path, byte_offset)) {
      continue;
    }
    if (best == nullptr ||
        (symbol.selection_range.end_offset - symbol.selection_range.start_offset) <
            (best->selection_range.end_offset - best->selection_range.start_offset)) {
      best = &symbol;
    }
  }
  return best;
}

const SymbolInfo* SymbolIndex::ResolveNameNear(
    const std::filesystem::path& path,
    std::size_t byte_offset,
    std::string_view name) const {
  if (const auto* direct = SymbolAt(path, byte_offset)) {
    if (direct->name == name) {
      return direct;
    }
  }

  std::string module_hint;
  for (const auto& symbol : symbols_) {
    if (Contains(symbol.range, path, byte_offset)) {
      module_hint = symbol.module_path;
      break;
    }
  }

  const SymbolInfo* fallback = nullptr;
  for (const auto& symbol : symbols_) {
    if (symbol.name != name) {
      continue;
    }
    if (!module_hint.empty() && symbol.module_path == module_hint) {
      return &symbol;
    }
    if (fallback == nullptr) {
      fallback = &symbol;
    }
  }
  return fallback;
}

SymbolIndex BuildSymbolIndex(const std::vector<ast::ASTModule>& modules) {
  SymbolIndex index;
  for (const auto& module : modules) {
    const std::string module_path = core::StringOfPath(module.path);
    for (const auto& item : module.items) {
      std::visit(
          [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, ast::ProcedureDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Function,
                        node.span, "procedure", DocText(node.doc));
              IndexBlock(index, module.path, node.body);
            } else if constexpr (std::is_same_v<T, ast::ComptimeProcedureDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Function,
                        node.span, "comptime procedure", DocText(node.doc));
              IndexBlock(index, module.path, node.body);
            } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
              if (const auto name = FirstPatternName(node.binding.pat)) {
                AddSymbol(index, module.path, *name, SymbolKind::Constant,
                          node.span, "module storage", DocText(node.doc));
              }
            } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Record,
                        node.span, "record", DocText(node.doc));
              for (const auto& member : node.members) {
                std::visit(
                    [&](const auto& member_node) {
                      using M = std::decay_t<decltype(member_node)>;
                      if constexpr (std::is_same_v<M, ast::FieldDecl>) {
                        AddSymbol(index, module.path, member_node.name,
                                  SymbolKind::Field, member_node.span,
                                  "record field", DocText(member_node.doc_opt));
                      } else if constexpr (std::is_same_v<M, ast::MethodDecl>) {
                        AddSymbol(index, module.path, member_node.name,
                                  SymbolKind::Method, member_node.span,
                                  "record method", DocText(member_node.doc_opt));
                        IndexBlock(index, module.path, member_node.body);
                      } else if constexpr (std::is_same_v<M, ast::AssociatedTypeDecl>) {
                        AddSymbol(index, module.path, member_node.name,
                                  SymbolKind::TypeAlias, member_node.span,
                                  "associated type", DocText(member_node.doc_opt));
                      }
                    },
                    member);
              }
            } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Enum,
                        node.span, "enum", DocText(node.doc));
              for (const auto& variant : node.variants) {
                AddSymbol(index, module.path, variant.name,
                          SymbolKind::EnumMember, variant.span,
                          "enum variant", DocText(variant.doc_opt));
              }
            } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Modal,
                        node.span, "modal", DocText(node.doc));
              for (const auto& state : node.states) {
                AddSymbol(index, module.path, state.name, SymbolKind::State,
                          state.span, "modal state", DocText(state.doc_opt));
              }
            } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::Class,
                        node.span, node.modal ? "modal class" : "class",
                        DocText(node.doc));
            } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
              AddSymbol(index, module.path, node.name, SymbolKind::TypeAlias,
                        node.span, "type alias", DocText(node.doc));
            }
          },
          item);
    }
  }
  return index;
}

std::string SymbolKindName(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::Module:
      return "module";
    case SymbolKind::Function:
      return "procedure";
    case SymbolKind::Method:
      return "method";
    case SymbolKind::Variable:
      return "variable";
    case SymbolKind::Constant:
      return "constant";
    case SymbolKind::Field:
      return "field";
    case SymbolKind::Record:
      return "record";
    case SymbolKind::Enum:
      return "enum";
    case SymbolKind::EnumMember:
      return "enum variant";
    case SymbolKind::Modal:
      return "modal";
    case SymbolKind::Class:
      return "class";
    case SymbolKind::TypeAlias:
      return "type alias";
    case SymbolKind::State:
      return "state";
  }
  return "symbol";
}

}  // namespace cursive::driver::tooling
