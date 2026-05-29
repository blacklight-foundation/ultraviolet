// =============================================================================
// parse_modules.cpp - Module-Level Parsing Orchestration
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 3.4.1-3.4.2 (Lines 6522-6582)
//
// This file implements module-level parsing:
//   - ReadBytesDefault: Read file bytes from filesystem
//   - ParseModuleWithDeps: Parse a single module with dependency injection
//   - ParseModulesWithDeps: Parse multiple modules in sequence
//   - ParseModule: Public entry point for single module
//   - ParseModules: Public entry point for multiple modules
//
// =============================================================================

#include "02_source/parser/parse_modules.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include "00_core/assert_spec.h"
#include "00_core/diagnostic_messages.h"
#include "00_core/process_config.h"
#include "00_core/diagnostics.h"
#include "00_core/host/services.h"
#include "00_core/host_primitives.h"
#include "02_source/lexer/keyword_policy.h"


namespace ultraviolet::frontend {

ParseModuleResult ParseModuleFromDirWithDeps(
    std::string_view module_path,
    const std::filesystem::path& module_dir,
    const ParseModuleDeps& deps);

// =============================================================================
// ReadBytesDefault - Default file reading implementation
// =============================================================================
//
// SPEC: ReadBytes-Ok (lines 6554-6557)
//   read_ok(f) = B
//   ----------------------------------------
//   ReadBytes(f) => B
//
// SPEC: ReadBytes-Err (lines 6559-6562)
//   read_ok(f) error    c = Code(ReadBytes-Err)
//   ----------------------------------------
//   ReadBytes(f) error c

ReadBytesResult ReadBytesDefault(const std::filesystem::path& path) {
  ReadBytesResult result;
  if (const auto force = core::HostGetEnvUtf8("UV_TEST_READ_BYTES_FAIL");
      force.has_value() && !force->empty()) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }

  const std::streamoff size_off = in.tellg();
  if (size_off < 0) {
    SPEC_RULE("ReadBytes-Err");
    core::HostPrimFail(core::HostPrim::ReadBytes, true);
    core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
    return result;
  }

  const std::size_t size = static_cast<std::size_t>(size_off);
  std::vector<std::uint8_t> bytes(size);
  in.seekg(0, std::ios::beg);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in) {
      SPEC_RULE("ReadBytes-Err");
      core::HostPrimFail(core::HostPrim::ReadBytes, true);
      core::EmitExternalDiagnostic(result.diags, "E-SRC-0102");
      return result;
    }
  }

  SPEC_RULE("ReadBytes-Ok");
  result.bytes = std::move(bytes);
  return result;
}

namespace {

// =============================================================================
// AppendDiags - Merge diagnostic streams
// =============================================================================

void AppendDiags(core::DiagnosticStream& out,
                 const core::DiagnosticStream& add) {
  for (const auto& diag : add) {
    core::Emit(out, diag);
  }
}

std::int64_t ElapsedMs(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - start)
      .count();
}

bool SameSpan(const core::Span& lhs, const core::Span& rhs) {
  return lhs.file == rhs.file && lhs.start_offset == rhs.start_offset &&
         lhs.end_offset == rhs.end_offset &&
         lhs.start_line == rhs.start_line && lhs.start_col == rhs.start_col &&
         lhs.end_line == rhs.end_line && lhs.end_col == rhs.end_col;
}

bool HasDiagnosticAt(const core::DiagnosticStream& diags,
                     std::string_view code,
                     const core::Span& span) {
  for (const auto& diag : diags) {
    if (diag.code != code || !diag.span.has_value()) {
      continue;
    }
    if (SameSpan(*diag.span, span)) {
      return true;
    }
  }
  return false;
}

void EmitReservedBinderKeywordErr(core::DiagnosticStream& diags,
                                  const ast::Identifier& name,
                                  const core::Span& span) {
  if (!lexer::IsKeyword(name)) {
    return;
  }
  if (HasDiagnosticAt(diags, "E-CNF-0401", span)) {
    return;
  }
  if (auto diag = core::MakeDiagnosticById("E-CNF-0401", span)) {
    core::Emit(diags, *diag);
  }
}

void ValidateReservedBinders(const ast::PatternPtr& pattern,
                             core::DiagnosticStream& diags);
void ValidateReservedBinders(const ast::ExprPtr& expr,
                             core::DiagnosticStream& diags);
void ValidateReservedBinders(const ast::BlockPtr& block,
                             core::DiagnosticStream& diags);

void ValidateReservedBinders(const std::optional<ast::GenericParams>& params,
                             core::DiagnosticStream& diags) {
  if (!params.has_value()) {
    return;
  }
  for (const auto& param : params->params) {
    EmitReservedBinderKeywordErr(diags, param.name, param.span);
  }
}

void ValidateReservedBinders(const std::vector<ast::Param>& params,
                             core::DiagnosticStream& diags) {
  for (const auto& param : params) {
    EmitReservedBinderKeywordErr(diags, param.name, param.span);
  }
}

void ValidateReservedBinders(const ast::Arg& arg,
                             core::DiagnosticStream& diags) {
  ValidateReservedBinders(arg.value, diags);
}

void ValidateReservedBinders(const ast::ApplyArgs& args,
                             core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::ParenArgs>) {
          for (const auto& arg : node.args) {
            ValidateReservedBinders(arg, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::BraceArgs>) {
          for (const auto& field : node.fields) {
            ValidateReservedBinders(field.value, diags);
          }
        }
      },
      args);
}

void ValidateReservedBinders(const std::optional<ast::EnumPayload>& payload,
                             core::DiagnosticStream& diags) {
  if (!payload.has_value()) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::EnumPayloadParen>) {
          for (const auto& elem : node.elements) {
            ValidateReservedBinders(elem, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPayloadBrace>) {
          for (const auto& field : node.fields) {
            ValidateReservedBinders(field.value, diags);
          }
        }
      },
      *payload);
}

void ValidateReservedBinders(const ast::FieldPattern& field,
                             core::DiagnosticStream& diags) {
  if (field.pattern_opt) {
    ValidateReservedBinders(field.pattern_opt, diags);
  } else {
    EmitReservedBinderKeywordErr(diags, field.name, field.span);
  }
}

void ValidateReservedBinders(const ast::PatternPtr& pattern,
                             core::DiagnosticStream& diags) {
  if (!pattern) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::IdentifierPattern>) {
          EmitReservedBinderKeywordErr(diags, node.name, pattern->span);
        } else if constexpr (std::is_same_v<T, ast::TypedPattern>) {
          EmitReservedBinderKeywordErr(diags, node.name, pattern->span);
        } else if constexpr (std::is_same_v<T, ast::TuplePattern>) {
          for (const auto& elem : node.elements) {
            ValidateReservedBinders(elem, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::RecordPattern>) {
          for (const auto& field : node.fields) {
            ValidateReservedBinders(field, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumPattern>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, ast::TuplePayloadPattern>) {
                    for (const auto& elem : payload.elements) {
                      ValidateReservedBinders(elem, diags);
                    }
                  } else if constexpr (std::is_same_v<P, ast::RecordPayloadPattern>) {
                    for (const auto& field : payload.fields) {
                      ValidateReservedBinders(field, diags);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ast::ModalPattern>) {
          if (node.fields_opt.has_value()) {
            for (const auto& field : node.fields_opt->fields) {
              ValidateReservedBinders(field, diags);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::RangePattern>) {
          ValidateReservedBinders(node.lo, diags);
          ValidateReservedBinders(node.hi, diags);
        }
      },
      pattern->node);
}

void ValidateReservedBinders(const ast::Stmt& stmt,
                             core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::LetStmt> ||
                      std::is_same_v<T, ast::VarStmt>) {
          ValidateReservedBinders(node.binding.pat, diags);
          ValidateReservedBinders(node.binding.init, diags);
        } else if constexpr (std::is_same_v<T, ast::UsingLocalStmt>) {
          EmitReservedBinderKeywordErr(diags, node.alias, node.span);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt> ||
                             std::is_same_v<T, ast::CompoundAssignStmt>) {
          ValidateReservedBinders(node.place, diags);
          ValidateReservedBinders(node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::ExprStmt>) {
          ValidateReservedBinders(node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::DeferStmt> ||
                             std::is_same_v<T, ast::FrameStmt> ||
                             std::is_same_v<T, ast::UnsafeBlockStmt> ||
                             std::is_same_v<T, ast::CtStmt> ||
                             std::is_same_v<T, ast::KeyBlockStmt>) {
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::RegionStmt>) {
          if (node.alias_opt.has_value()) {
            EmitReservedBinderKeywordErr(diags, *node.alias_opt, node.span);
          }
          ValidateReservedBinders(node.opts_opt, diags);
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt> ||
                             std::is_same_v<T, ast::BreakStmt>) {
          ValidateReservedBinders(node.value_opt, diags);
        }
      },
      stmt);
}

void ValidateReservedBinders(const ast::BlockPtr& block,
                             core::DiagnosticStream& diags) {
  if (!block) {
    return;
  }
  for (const auto& stmt : block->stmts) {
    ValidateReservedBinders(stmt, diags);
  }
  ValidateReservedBinders(block->tail_opt, diags);
}

void ValidateReservedBinders(const ast::ExprPtr& expr,
                             core::DiagnosticStream& diags) {
  if (!expr) {
    return;
  }
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::QualifiedApplyExpr>) {
          ValidateReservedBinders(node.args, diags);
        } else if constexpr (std::is_same_v<T, ast::RangeExpr>) {
          ValidateReservedBinders(node.lhs, diags);
          ValidateReservedBinders(node.rhs, diags);
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
          ValidateReservedBinders(node.lhs, diags);
          ValidateReservedBinders(node.rhs, diags);
        } else if constexpr (std::is_same_v<T, ast::ArrayRepeatExpr>) {
          ValidateReservedBinders(node.value, diags);
          ValidateReservedBinders(node.count, diags);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr> ||
                             std::is_same_v<T, ast::DerefExpr> ||
                             std::is_same_v<T, ast::AddressOfExpr> ||
                             std::is_same_v<T, ast::MoveExpr> ||
                             std::is_same_v<T, ast::AllocExpr> ||
                             std::is_same_v<T, ast::SizeofExpr> ||
                             std::is_same_v<T, ast::AlignofExpr> ||
                             std::is_same_v<T, ast::PropagateExpr> ||
                             std::is_same_v<T, ast::EntryExpr> ||
                             std::is_same_v<T, ast::YieldExpr> ||
                             std::is_same_v<T, ast::YieldFromExpr> ||
                             std::is_same_v<T, ast::SyncExpr> ||
                             std::is_same_v<T, ast::WaitExpr>) {
          if constexpr (requires { node.value; }) {
            ValidateReservedBinders(node.value, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::CastExpr> ||
                             std::is_same_v<T, ast::TransmuteExpr>) {
          ValidateReservedBinders(node.value, diags);
        } else if constexpr (std::is_same_v<T, ast::TupleExpr>) {
          for (const auto& elem : node.elements) {
            ValidateReservedBinders(elem, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::ArrayExpr>) {
          ast::ForEachArrayExprSubexpr(node, [&](const ast::ExprPtr& elem) {
            ValidateReservedBinders(elem, diags);
          });
        } else if constexpr (std::is_same_v<T, ast::RecordExpr>) {
          for (const auto& field : node.fields) {
            ValidateReservedBinders(field.value, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumLiteralExpr>) {
          ValidateReservedBinders(node.payload_opt, diags);
        } else if constexpr (std::is_same_v<T, ast::IfExpr>) {
          ValidateReservedBinders(node.cond, diags);
          ValidateReservedBinders(node.then_expr, diags);
          ValidateReservedBinders(node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::IfIsExpr>) {
          ValidateReservedBinders(node.scrutinee, diags);
          ValidateReservedBinders(node.pattern, diags);
          ValidateReservedBinders(node.then_expr, diags);
          ValidateReservedBinders(node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::IfCaseExpr>) {
          ValidateReservedBinders(node.scrutinee, diags);
          for (const auto& clause : node.cases) {
            ValidateReservedBinders(clause.pattern, diags);
            ValidateReservedBinders(clause.body, diags);
          }
          ValidateReservedBinders(node.else_expr, diags);
        } else if constexpr (std::is_same_v<T, ast::LoopInfiniteExpr>) {
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::LoopConditionalExpr>) {
          ValidateReservedBinders(node.cond, diags);
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::LoopIterExpr>) {
          ValidateReservedBinders(node.pattern, diags);
          ValidateReservedBinders(node.iter, diags);
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::BlockExpr> ||
                             std::is_same_v<T, ast::UnsafeBlockExpr>) {
          ValidateReservedBinders(node.block, diags);
        } else if constexpr (std::is_same_v<T, ast::ComptimeExpr> ||
                             std::is_same_v<T, ast::AttributedExpr> ||
                             std::is_same_v<T, ast::PipelineExpr>) {
          if constexpr (requires { node.body; }) {
            ValidateReservedBinders(node.body, diags);
          }
          if constexpr (requires { node.expr; }) {
            ValidateReservedBinders(node.expr, diags);
          }
          if constexpr (requires { node.lhs; }) {
            ValidateReservedBinders(node.lhs, diags);
            ValidateReservedBinders(node.rhs, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::CtIfExpr>) {
          ValidateReservedBinders(node.cond, diags);
          ValidateReservedBinders(node.then_block, diags);
          ValidateReservedBinders(node.else_block_opt, diags);
        } else if constexpr (std::is_same_v<T, ast::CtLoopIterExpr>) {
          ValidateReservedBinders(node.pattern, diags);
          ValidateReservedBinders(node.iter, diags);
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::ClosureExpr>) {
          for (const auto& param : node.params) {
            EmitReservedBinderKeywordErr(diags, param.name, expr->span);
          }
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::FieldAccessExpr> ||
                             std::is_same_v<T, ast::TupleAccessExpr>) {
          ValidateReservedBinders(node.base, diags);
        } else if constexpr (std::is_same_v<T, ast::IndexAccessExpr>) {
          ValidateReservedBinders(node.base, diags);
          ValidateReservedBinders(node.index, diags);
        } else if constexpr (std::is_same_v<T, ast::CallExpr> ||
                             std::is_same_v<T, ast::CallTypeArgsExpr>) {
          ValidateReservedBinders(node.callee, diags);
          for (const auto& arg : node.args) {
            ValidateReservedBinders(arg, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::MethodCallExpr>) {
          ValidateReservedBinders(node.receiver, diags);
          for (const auto& arg : node.args) {
            ValidateReservedBinders(arg, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::RaceExpr>) {
          for (const auto& arm : node.arms) {
            ValidateReservedBinders(arm.expr, diags);
            ValidateReservedBinders(arm.pattern, diags);
            ValidateReservedBinders(arm.handler.value, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::AllExpr>) {
          for (const auto& elem : node.exprs) {
            ValidateReservedBinders(elem, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::ParallelExpr>) {
          ValidateReservedBinders(node.domain, diags);
          for (const auto& opt : node.opts) {
            ValidateReservedBinders(opt.value, diags);
          }
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::SpawnExpr>) {
          for (const auto& opt : node.opts) {
            ValidateReservedBinders(opt.value, diags);
          }
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::DispatchExpr>) {
          ValidateReservedBinders(node.pattern, diags);
          ValidateReservedBinders(node.range, diags);
          for (const auto& opt : node.opts) {
            if (opt.kind == ast::DispatchOptionKind::Chunk) {
              ValidateReservedBinders(opt.chunk_expr, diags);
            } else if (opt.kind == ast::DispatchOptionKind::Workgroup) {
              ValidateReservedBinders(opt.workgroup_expr, diags);
            }
          }
          ValidateReservedBinders(node.body, diags);
        }
      },
      expr->node);
}

void ValidateReservedBinders(const ast::FieldDecl& field,
                             core::DiagnosticStream& diags) {
  EmitReservedBinderKeywordErr(diags, field.name, field.span);
  ValidateReservedBinders(field.init_opt, diags);
}

void ValidateReservedBinders(const ast::MethodDecl& method,
                             core::DiagnosticStream& diags) {
  EmitReservedBinderKeywordErr(diags, method.name, method.span);
  ValidateReservedBinders(method.generic_params, diags);
  ValidateReservedBinders(method.params, diags);
  ValidateReservedBinders(method.body, diags);
}

void ValidateReservedBinders(const ast::AssociatedTypeDecl& assoc,
                             core::DiagnosticStream& diags) {
  EmitReservedBinderKeywordErr(diags, assoc.name, assoc.span);
}

void ValidateReservedBinders(const ast::ClassItem& item,
                             core::DiagnosticStream& diags);

void ValidateReservedBinders(const ast::ASTItem& item,
                             core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ast::UsingDecl>) {
          std::visit(
              [&](const auto& clause) {
                using C = std::decay_t<decltype(clause)>;
                if constexpr (std::is_same_v<C, ast::UsingItem>) {
                  EmitReservedBinderKeywordErr(diags,
                                               clause.alias_opt.value_or(clause.name),
                                               node.span);
                } else if constexpr (std::is_same_v<C, ast::UsingList>) {
                  for (const auto& spec : clause.specs) {
                    EmitReservedBinderKeywordErr(
                        diags, spec.alias_opt.value_or(spec.name), node.span);
                  }
                }
              },
              node.clause);
        } else if constexpr (std::is_same_v<T, ast::ImportDecl>) {
          if (node.alias_opt.has_value()) {
            EmitReservedBinderKeywordErr(diags, *node.alias_opt, node.span);
          }
        } else if constexpr (std::is_same_v<T, ast::ExternBlock>) {
          for (const auto& extern_item : node.items) {
            std::visit(
                [&](const auto& ext) {
                  EmitReservedBinderKeywordErr(diags, ext.name, ext.span);
                  ValidateReservedBinders(ext.generic_params, diags);
                  ValidateReservedBinders(ext.params, diags);
                },
                extern_item);
          }
        } else if constexpr (std::is_same_v<T, ast::StaticDecl>) {
          ValidateReservedBinders(node.binding.pat, diags);
          ValidateReservedBinders(node.binding.init, diags);
        } else if constexpr (std::is_same_v<T, ast::ProcedureDecl> ||
                             std::is_same_v<T, ast::ComptimeProcedureDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
          ValidateReservedBinders(node.params, diags);
          ValidateReservedBinders(node.body, diags);
        } else if constexpr (std::is_same_v<T, ast::RecordDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
          for (const auto& member : node.members) {
            std::visit(
                [&](const auto& record_member) {
                  ValidateReservedBinders(record_member, diags);
                },
                member);
          }
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
          for (const auto& variant : node.variants) {
            EmitReservedBinderKeywordErr(diags, variant.name, variant.span);
            if (variant.payload_opt.has_value()) {
              std::visit(
                  [&](const auto& payload) {
                    using P = std::decay_t<decltype(payload)>;
                    if constexpr (std::is_same_v<P, ast::VariantPayloadRecord>) {
                      for (const auto& field : payload.fields) {
                        ValidateReservedBinders(field, diags);
                      }
                    }
                  },
                  *variant.payload_opt);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ModalDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
          for (const auto& state : node.states) {
            EmitReservedBinderKeywordErr(diags, state.name, state.span);
            for (const auto& member : state.members) {
              std::visit(
                  [&](const auto& state_member) {
                    EmitReservedBinderKeywordErr(diags, state_member.name,
                                                 state_member.span);
                    if constexpr (requires { state_member.generic_params; }) {
                      ValidateReservedBinders(state_member.generic_params, diags);
                    }
                    if constexpr (requires { state_member.params; }) {
                      ValidateReservedBinders(state_member.params, diags);
                    }
                    if constexpr (requires { state_member.body; }) {
                      ValidateReservedBinders(state_member.body, diags);
                    }
                  },
                  member);
            }
          }
        } else if constexpr (std::is_same_v<T, ast::ClassDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
          for (const auto& class_item : node.items) {
            ValidateReservedBinders(class_item, diags);
          }
        } else if constexpr (std::is_same_v<T, ast::TypeAliasDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.generic_params, diags);
        } else if constexpr (std::is_same_v<T, ast::DeriveTargetDecl>) {
          EmitReservedBinderKeywordErr(diags, node.name, node.span);
          ValidateReservedBinders(node.body, diags);
        }
      },
      item);
}

void ValidateReservedBinders(const ast::ClassItem& item,
                             core::DiagnosticStream& diags) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        EmitReservedBinderKeywordErr(diags, node.name, node.span);
        if constexpr (std::is_same_v<T, ast::ClassMethodDecl>) {
          ValidateReservedBinders(node.generic_params, diags);
          ValidateReservedBinders(node.params, diags);
          ValidateReservedBinders(node.body_opt, diags);
        } else if constexpr (std::is_same_v<T, ast::AbstractStateDecl>) {
          for (const auto& field : node.fields) {
            EmitReservedBinderKeywordErr(diags, field.name, field.span);
          }
        }
      },
      item);
}

void ValidateReservedBinders(const ast::ASTFile& file,
                             core::DiagnosticStream& diags) {
  for (const auto& item : file.items) {
    ValidateReservedBinders(item, diags);
  }
}

// Enforce MethodContextOk at parse/module aggregation time for top-level
// procedures: a top-level procedure parameter named `self` is method-only.
void CheckMethodContext(const ast::ASTFile& file, core::DiagnosticStream& diags) {
  for (const auto& item : file.items) {
    const auto* proc = std::get_if<ast::ProcedureDecl>(&item);
    if (!proc) {
      continue;
    }
    for (const auto& param : proc->params) {
      if (param.name != "self") {
        continue;
      }
      if (auto diag = core::MakeDiagnosticById(
              "E-SEM-3011", std::optional<core::Span>(param.span))) {
        core::Emit(diags, *diag);
      }
      break;
    }
  }
}

// =============================================================================
// SplitModulePath - Split "a::b::c" into ["a", "b", "c"]
// =============================================================================

std::vector<std::string> SplitModulePath(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t pos = path.find("::", start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(path.substr(start));
      break;
    }
    parts.emplace_back(path.substr(start, pos - start));
    start = pos + 2;
  }
  return parts;
}

// =============================================================================
// DirOf - Compute directory path for a module
// =============================================================================
//
// SPEC: DirOf-Root (lines 6537-6540)
//   p = A
//   ----------------------------------------
//   DirOf(p, S) = S
//
// SPEC: DirOf-Rel (lines 6542-6545)
//   p = c_1 :: ... :: c_n    n >= 1
//   ----------------------------------------
//   DirOf(p, S) = S / c_1 / ... / c_n

std::filesystem::path DirOf(std::string_view module_path,
                            const std::filesystem::path& source_root,
                            std::string_view assembly_name) {
  if (module_path == assembly_name) {
    SPEC_RULE("DirOf-Root");
    return source_root;
  }
  SPEC_RULE("DirOf-Rel");
  const auto comps = SplitModulePath(module_path);
  std::filesystem::path dir = source_root;
  std::size_t start = 0;
  // Module paths may be assembly-qualified for cross-assembly uniqueness.
  // Directory layout under source_root remains relative to the assembly root.
  if (!comps.empty() && comps.front() == assembly_name) {
    start = 1;
  }
  for (std::size_t i = start; i < comps.size(); ++i) {
    dir /= comps[i];
  }
  return dir;
}

struct ParseModuleJob {
  ParseModuleResult parsed;
  std::vector<ParseFileTelemetry> file_telemetry;
  std::vector<ParseModuleTelemetry> module_telemetry;
  std::exception_ptr exception;
};

bool CanParseModulesInParallel(const std::vector<project::ModuleInfo>& modules,
                               const ParseModuleDeps& deps) {
  if (!deps.parallel_modules || modules.size() < 2) {
    return false;
  }
  if (core::Conformance::Enabled()) {
    return false;
  }
  if (core::IsDebugEnabled("phases") || core::IsDebugEnabled("parse")) {
    return false;
  }
  return true;
}

std::size_t ParseModuleWorkerCount(std::size_t job_count) {
  const unsigned int hardware_threads = std::thread::hardware_concurrency();
  const std::size_t available_threads =
      hardware_threads == 0 ? 2 : static_cast<std::size_t>(hardware_threads);
  return std::max<std::size_t>(
      1, std::min<std::size_t>(job_count, available_threads));
}

void EmitCapturedTelemetry(const ParseModuleDeps& deps,
                           const ParseModuleJob& job) {
  if (deps.file_telemetry) {
    for (const auto& record : job.file_telemetry) {
      deps.file_telemetry(record);
    }
  }
  if (deps.module_telemetry) {
    for (const auto& record : job.module_telemetry) {
      deps.module_telemetry(record);
    }
  }
}

ParseModulesResult FinishParsedModuleJobs(
    std::vector<ParseModuleJob>& jobs,
    const ParseModuleDeps& deps) {
  ParseModulesResult result;

  std::vector<ast::ASTModule> parsed_modules;
  parsed_modules.reserve(jobs.size());
  for (auto& job : jobs) {
    if (job.exception) {
      std::rethrow_exception(job.exception);
    }

    EmitCapturedTelemetry(deps, job);
    AppendDiags(result.diags, job.parsed.diags);
    for (auto& [path, spans] : job.parsed.unsafe_spans_by_file) {
      result.unsafe_spans_by_file.insert_or_assign(std::move(path),
                                                   std::move(spans));
    }
    if (!job.parsed.module.has_value()) {
      SPEC_RULE("ParseModules-Err");
      return result;
    }
    parsed_modules.push_back(std::move(*job.parsed.module));
  }

  SPEC_RULE("ParseModules-Ok");
  SPEC_RULE("Phase1-Complete");
  SPEC_RULE("Phase1-Declarations");
  SPEC_RULE("Phase1-Forward-Refs");
  result.modules = std::move(parsed_modules);
  return result;
}

ParseModulesResult ParseModulesParallel(
    const std::vector<project::ModuleInfo>& modules,
    const std::filesystem::path& source_root,
    std::string_view assembly_name,
    const ParseModuleDeps& deps) {
  std::vector<ParseModuleJob> jobs(modules.size());
  std::atomic<std::size_t> next_job{0};
  const std::size_t worker_count = ParseModuleWorkerCount(modules.size());
  std::vector<std::thread> workers;
  workers.reserve(worker_count);

  for (std::size_t worker_index = 0; worker_index < worker_count;
       ++worker_index) {
    workers.emplace_back([&]() {
      while (true) {
        const std::size_t index =
            next_job.fetch_add(1, std::memory_order_relaxed);
        if (index >= modules.size()) {
          return;
        }

        ParseModuleJob& job = jobs[index];
        try {
          ParseModuleDeps local_deps = deps;
          local_deps.file_telemetry =
              [&job](const ParseFileTelemetry& record) {
                job.file_telemetry.push_back(record);
              };
          local_deps.module_telemetry =
              [&job](const ParseModuleTelemetry& record) {
                job.module_telemetry.push_back(record);
              };

          const auto& module = modules[index];
          const std::filesystem::path module_dir =
              module.dir.empty() ? DirOf(module.path, source_root, assembly_name)
                                 : module.dir;
          job.parsed =
              ParseModuleFromDirWithDeps(module.path, module_dir, local_deps);
        } catch (...) {
          job.exception = std::current_exception();
        }
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }

  return FinishParsedModuleJobs(jobs, deps);
}

// =============================================================================
// DefaultDeps - Create default ParseModuleDeps
// =============================================================================

ParseModuleDeps DefaultDeps() {
  ParseModuleDeps deps;
  deps.compilation_unit = static_cast<project::CompilationUnitResult (*)(
      const std::filesystem::path&)>(project::CompilationUnit);
  deps.read_bytes = ReadBytesDefault;
  deps.load_source = core::LoadSource;
  deps.parse_file = ast::ParseFile;
  return deps;
}

}  // namespace

// =============================================================================
// ParseModuleWithDeps - Parse a single module with dependency injection
// =============================================================================
//
// SPEC: ParseModule-Ok (lines 6566-6569)
//   forall i, ReadBytes(f_i) => B_i    LoadSource(f_i, B_i) => S_i    ParseFile(S_i) => F_i
//   ----------------------------------------
//   ParseModule(p, S) => <p, F_1.items ++ ... ++ F_n.items, F_1.module_doc ++ ... ++ F_n.module_doc>
//
// SPEC: ParseModule-Err-Read (lines 6571-6574)
//   exists i, ReadBytes(f_i) error c
//   ----------------------------------------
//   ParseModule(p, S) error c
//
// SPEC: ParseModule-Err-Load (lines 6576-6579)
//   exists i, ReadBytes(f_i) => B_i    LoadSource(f_i, B_i) error c
//   ----------------------------------------
//   ParseModule(p, S) error c

ParseModuleResult ParseModuleFromDirWithDeps(
    std::string_view module_path,
    const std::filesystem::path& module_dir,
    const ParseModuleDeps& deps) {
  ParseModuleResult result;
  const bool debug_phases = core::IsDebugEnabled("phases");
  const auto log_phase = [&](const char* label,
                             const std::filesystem::path& path) {
    if (debug_phases) {
      std::cerr << "[uv] parse: " << label << " " << path.string()
                << "\n";
    }
  };

  const auto module_start = std::chrono::steady_clock::now();
  ParseModuleTelemetry module_telemetry;
  module_telemetry.module_path = std::string(module_path);
  module_telemetry.module_dir = module_dir;
  bool module_telemetry_emitted = false;
  const auto emit_module_telemetry = [&](bool ok) {
    if (!deps.module_telemetry || module_telemetry_emitted) {
      return;
    }
    module_telemetry.ok = ok;
    module_telemetry.total_ms = ElapsedMs(module_start);
    module_telemetry.diagnostics = result.diags.size();
    deps.module_telemetry(module_telemetry);
    module_telemetry_emitted = true;
  };

  SPEC_RULE("Mod-Start");

  const auto compilation_unit_start = std::chrono::steady_clock::now();
  const project::CompilationUnitResult unit = deps.compilation_unit(module_dir);
  module_telemetry.compilation_unit_ms = ElapsedMs(compilation_unit_start);
  module_telemetry.file_count = unit.files.size();
  AppendDiags(result.diags, unit.diags);
  if (core::HasError(unit.diags)) {
    SPEC_RULE("Mod-Start-Err-Unit");
    SPEC_RULE("ParseModule-Err-Unit");
    emit_module_telemetry(false);
    return result;
  }

  std::vector<ast::ASTItem> items;
  std::vector<lexer::DocComment> docs;
  for (const auto& file : unit.files) {
    const auto file_start = std::chrono::steady_clock::now();
    ParseFileTelemetry file_telemetry;
    file_telemetry.module_path = std::string(module_path);
    file_telemetry.file_path = file;
    const auto emit_file_telemetry = [&](bool ok) {
      if (!deps.file_telemetry) {
        return;
      }
      file_telemetry.ok = ok;
      file_telemetry.total_ms = ElapsedMs(file_start);
      deps.file_telemetry(file_telemetry);
    };

    log_phase("read", file);
    const auto read_start = std::chrono::steady_clock::now();
    const ReadBytesResult bytes = deps.read_bytes(file);
    file_telemetry.read_ms = ElapsedMs(read_start);
    file_telemetry.diagnostics += bytes.diags.size();
    AppendDiags(result.diags, bytes.diags);
    if (!bytes.bytes.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Read");
      SPEC_RULE("ParseModule-Err-Read");
      emit_file_telemetry(false);
      emit_module_telemetry(false);
      return result;
    }
    file_telemetry.byte_count = bytes.bytes->size();
    module_telemetry.byte_count += bytes.bytes->size();
    const auto load_start = std::chrono::steady_clock::now();
    const core::SourceLoadResult load =
        deps.load_source(file.generic_string(), *bytes.bytes);
    file_telemetry.load_ms = ElapsedMs(load_start);
    file_telemetry.diagnostics += load.diags.size();
    AppendDiags(result.diags, load.diags);
    if (!load.source.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Load");
      SPEC_RULE("ParseModule-Err-Load");
      emit_file_telemetry(false);
      emit_module_telemetry(false);
      return result;
    }

    core::DiagnosticStream inspect_diags;
    if (deps.inspect_source) {
      log_phase("inspect", file);
      const auto inspect_start = std::chrono::steady_clock::now();
      inspect_diags = deps.inspect_source(*load.source);
      file_telemetry.inspect_ms = ElapsedMs(inspect_start);
      file_telemetry.diagnostics += inspect_diags.size();
    }

    log_phase("parse", file);
    const auto parse_start = std::chrono::steady_clock::now();
    ast::ParseFileResult parsed = deps.parse_file(*load.source);
    file_telemetry.parse_ms = ElapsedMs(parse_start);
    file_telemetry.diagnostics += parsed.diags.size();
    if (core::IsDebugEnabled("parse")) {
      std::cerr << "[uv] parse: file=" << file.string()
                << " diags=" << parsed.diags.size()
                << " ok=" << (parsed.file.has_value() ? "yes" : "no") << "\n";
    }
    AppendDiags(result.diags, parsed.diags);
    AppendDiags(result.diags, inspect_diags);
    if (!parsed.file.has_value()) {
      SPEC_RULE("Mod-Scan-Err-Parse");
      SPEC_RULE("ParseModule-Err-Parse");
      emit_file_telemetry(false);
      emit_module_telemetry(false);
      return result;
    }

    CheckMethodContext(*parsed.file, result.diags);
    ValidateReservedBinders(*parsed.file, result.diags);

    SPEC_RULE("Mod-Scan");
    items.reserve(items.size() + parsed.file->items.size());
    items.insert(items.end(),
                 std::make_move_iterator(parsed.file->items.begin()),
                 std::make_move_iterator(parsed.file->items.end()));
    docs.reserve(docs.size() + parsed.file->module_doc.size());
    docs.insert(docs.end(),
                std::make_move_iterator(parsed.file->module_doc.begin()),
                std::make_move_iterator(parsed.file->module_doc.end()));
    result.unsafe_spans_by_file[load.source->path] =
        std::move(parsed.unsafe_spans);
    emit_file_telemetry(true);
  }

  SPEC_RULE("Mod-Done");
  ast::ASTModule module;
  module.path = SplitModulePath(module_path);
  module.items = std::move(items);
  module.module_doc = std::move(docs);
  result.module = std::move(module);
  SPEC_RULE("ParseModule-Ok");
  emit_module_telemetry(true);
  return result;
}

ParseModuleResult ParseModuleWithDeps(std::string_view module_path,
                                      const std::filesystem::path& source_root,
                                      std::string_view assembly_name,
                                      const ParseModuleDeps& deps) {
  const std::filesystem::path module_dir =
      DirOf(module_path, source_root, assembly_name);
  return ParseModuleFromDirWithDeps(module_path, module_dir, deps);
}

// =============================================================================
// ParseModulesWithDeps - Parse multiple modules in sequence
// =============================================================================
//
// SPEC: ParseModules-Ok (line 257), ParseModules-Err (line 251)

ParseModulesResult ParseModulesWithDeps(
    const std::vector<project::ModuleInfo>& modules,
    const std::filesystem::path& source_root,
    std::string_view assembly_name,
    const ParseModuleDeps& deps) {
  if (CanParseModulesInParallel(modules, deps)) {
    return ParseModulesParallel(modules, source_root, assembly_name, deps);
  }

  ParseModulesResult result;

  std::vector<ast::ASTModule> parsed_modules;
  parsed_modules.reserve(modules.size());
  for (const auto& module : modules) {
    const std::filesystem::path module_dir =
        module.dir.empty() ? DirOf(module.path, source_root, assembly_name)
                           : module.dir;
    ParseModuleResult parsed =
        ParseModuleFromDirWithDeps(module.path, module_dir, deps);
    AppendDiags(result.diags, parsed.diags);
    for (auto& [path, spans] : parsed.unsafe_spans_by_file) {
      result.unsafe_spans_by_file.insert_or_assign(std::move(path),
                                                   std::move(spans));
    }
    if (!parsed.module.has_value()) {
      SPEC_RULE("ParseModules-Err");
      return result;
    }
    parsed_modules.push_back(std::move(*parsed.module));
  }

  SPEC_RULE("ParseModules-Ok");
  SPEC_RULE("Phase1-Complete");
  SPEC_RULE("Phase1-Declarations");
  SPEC_RULE("Phase1-Forward-Refs");
  result.modules = std::move(parsed_modules);
  return result;
}

// =============================================================================
// ParseModule - Public entry point for single module
// =============================================================================

ParseModuleResult ParseModule(std::string_view module_path,
                              const std::filesystem::path& source_root,
                              std::string_view assembly_name) {
  return ParseModuleWithDeps(module_path, source_root, assembly_name,
                             DefaultDeps());
}

// =============================================================================
// ParseModules - Public entry point for multiple modules
// =============================================================================

ParseModulesResult ParseModules(const std::vector<project::ModuleInfo>& modules,
                                const std::filesystem::path& source_root,
                                std::string_view assembly_name) {
  return ParseModulesWithDeps(modules, source_root, assembly_name,
                              DefaultDeps());
}

}  // namespace ultraviolet::frontend
