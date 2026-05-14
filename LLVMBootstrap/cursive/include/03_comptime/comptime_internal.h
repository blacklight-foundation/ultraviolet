#pragma once

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "00_core/diagnostics.h"
#include "02_source/ast/ast.h"
#include "02_source/module_paths.h"
#include "02_source/parser/parser.h"
#include "03_comptime/comptime.h"

namespace cursive::frontend::comptime_internal {

using ASTItem = ast::ASTItem;
using AttributeItem = ast::AttributeItem;
using AttributeList = ast::AttributeList;
using Block = ast::Block;
using BlockPtr = ast::BlockPtr;
using ComptimeProcedureDecl = ast::ComptimeProcedureDecl;
using Expr = ast::Expr;
using ExprPtr = ast::ExprPtr;
using Identifier = ast::Identifier;
using Path = ast::Path;
using PatternPtr = ast::PatternPtr;
using QuoteExpr = ast::QuoteExpr;
using Stmt = ast::Stmt;
using TypePath = ast::TypePath;
using TypePtr = ast::TypePtr;

enum class CtPrimKind {
  Unit,
  Bool,
  Int,
  Float,
  Char,
};

struct CtPrimInt {
  unsigned long long value = 0;
  std::string suffix = "u64";
  std::string lexeme;
};

struct CtPrimFloat {
  std::string lexeme;
};

struct CtPrimChar {
  std::string lexeme;
};

struct CtPrim {
  CtPrimKind kind = CtPrimKind::Unit;
  std::variant<std::monostate, bool, CtPrimInt, CtPrimFloat, CtPrimChar> value;
};
struct CtString {
  std::string value;
};
struct CtBytes {
  std::string value;
};
struct CtType {
  ast::TypePtr type;
};

enum class CtAstKind {
  Expr,
  Stmt,
  Item,
  Type,
  Pattern,
};

struct CtSite {
  ast::ModulePath module_path;
  std::size_t ordinal = 0;
  core::Span span;
};

struct CtHygiene {
  CtSite quote_site;
  CtSite emit_site;
  std::size_t mark = 0;
};

struct CtAst {
  CtAstKind kind = CtAstKind::Expr;
  std::variant<ExprPtr, Stmt, ASTItem, TypePtr, PatternPtr> payload;
  std::optional<core::Span> span;
  std::optional<CtHygiene> hygiene;
};

struct CtTuple;
struct CtArray;
struct CtSlice;
struct CtRecord;
struct CtModalState;
struct CtEnum;

using CtValue = std::variant<CtPrim, CtString, CtBytes, CtType,
                             CtAst, std::shared_ptr<CtTuple>,
                             std::shared_ptr<CtArray>, std::shared_ptr<CtSlice>,
                             std::shared_ptr<CtRecord>,
                             std::shared_ptr<CtModalState>,
                             std::shared_ptr<CtEnum>>;

struct CtTuple {
  std::vector<CtValue> elements;
};

struct CtArray {
  std::vector<CtValue> elements;
};

struct CtSlice {
  std::vector<CtValue> elements;
};

struct CtTuplePayload {
  std::vector<CtValue> elements;
};

struct CtRecordPayload {
  std::vector<std::pair<Identifier, CtValue>> fields;
};

using CtPayload = std::variant<std::monostate, CtTuplePayload, CtRecordPayload>;

struct CtRecord {
  Path path;
  std::vector<std::pair<Identifier, CtValue>> fields;
};

struct CtModalState {
  ast::ModalStateRef target;
  std::vector<std::pair<Identifier, CtValue>> fields;
};

struct CtEnum {
  Path path;
  Identifier variant;
  CtPayload payload;
};

struct EvalResult {
  bool ok = false;
  CtValue value;
  bool returned = false;
};

enum class ProjectFileSnapshotKind {
  File,
  Directory,
  Other,
};

struct ProjectFileSnapshotEntry {
  ProjectFileSnapshotKind kind = ProjectFileSnapshotKind::Other;
  std::optional<std::string> exists_error;
  std::optional<std::string> read_error;
  std::optional<std::string> read_bytes_error;
  std::optional<std::string> list_dir_error;
  std::vector<std::uint8_t> bytes;
  std::vector<std::string> dir_entries;
};

struct ProjectFileSnapshot {
  std::string root_text;
  std::filesystem::path host_root;
  std::unordered_map<std::string, ProjectFileSnapshotEntry> entries;
  std::uint64_t captured_file_count = 0;
  std::uint64_t captured_directory_count = 0;
  std::uint64_t captured_byte_count = 0;
};

struct CtQuoteCtx {
  ast::QuoteKind kind = ast::QuoteKind::Unspecified;
  CtSite quote_site;
};

struct CtEnv {
  std::unordered_map<std::string, CtValue> values;
  std::unordered_map<std::string, ComptimeProcedureDecl> procs;
  std::vector<std::string> caps;
  CtSite site;
  std::optional<CtQuoteCtx> quote_ctx;
  std::vector<const ast::ASTModule*> available_modules;
  source::ModuleNames available_module_names;
  const std::vector<ASTItem>* current_module_items = nullptr;
  std::vector<ASTItem>* pending_emits = nullptr;
  core::DiagnosticStream* diags = nullptr;
  std::filesystem::path project_root;
  std::filesystem::path source_root;
  std::size_t next_hygiene = 0;
  ast::ModulePath current_module;
  std::size_t current_item_index = 0;
  core::Span current_span;
  std::shared_ptr<const std::unordered_map<std::string, CtValue>>
      contract_entry_values;
  std::optional<CtValue> contract_result_value;
  std::shared_ptr<const ProjectFileSnapshot> files;
  std::optional<ast::QuoteKind> return_quote_kind;
};

void AppendDiags(core::DiagnosticStream& out, const core::DiagnosticStream& add);
void EmitComptimeDiag(CtEnv& env,
                      std::string_view diag_id,
                      const core::Span& span);

bool HasAttribute(const AttributeList& attrs, std::string_view name);
const AttributeItem* FindAttribute(const AttributeList& attrs,
                                   std::string_view name);
AttributeList StripAttribute(const AttributeList& attrs,
                             std::string_view name);
CtEnv CtEmptyEnv(const ast::ASTModule& module);
CtEnv WithCtCaps(CtEnv env, const AttributeList& attrs, bool derive_body = false);
CtEnv WithCtSite(CtEnv env, std::size_t ord, const core::Span& sp);
CtEnv BindCtProc(CtEnv env, const ComptimeProcedureDecl& proc);

const std::unordered_map<std::string, CtValue>& CtVals(const CtEnv& env);
const std::unordered_map<std::string, ComptimeProcedureDecl>& CtProcs(
    const CtEnv& env);
const std::vector<std::string>& CtCaps(const CtEnv& env);
const CtSite& CtSiteOf(const CtEnv& env);
const std::optional<CtQuoteCtx>& CtQuoteCtxOf(const CtEnv& env);
std::shared_ptr<const ProjectFileSnapshot> CtFiles(const CtEnv& env);
const std::filesystem::path& CtProjectRoot(const CtEnv& env);
core::DiagnosticStream* CtDiags(const CtEnv& env);
std::vector<ASTItem>* CtPendingEmits(const CtEnv& env);
std::size_t CtFreshSeed(const CtEnv& env);
std::size_t TakeFreshSeed(CtEnv& env);
CtAstKind AstKindOf(const CtAst& ast);
const std::variant<ExprPtr, Stmt, ASTItem, TypePtr, PatternPtr>& AstPayloadOf(
    const CtAst& ast);
std::optional<core::Span> AstSpanOf(const CtAst& ast);
const std::optional<CtHygiene>& AstHygieneOf(const CtAst& ast);
CtAst AstOf(CtAstKind kind,
            std::variant<ExprPtr, Stmt, ASTItem, TypePtr, PatternPtr> payload);
std::optional<CtAst> HygienizeAst(const CtAst& ast,
                                  const CtSite& quote_site,
                                  const CtSite& emit_site,
                                  std::size_t seed,
                                  std::size_t& next_seed,
                                  CtEnv& env);
std::optional<CtAst> PrepareAstForInsertion(const CtAst& ast,
                                            const CtSite& emit_site,
                                            CtEnv& env);
CtValue MakeCtUnit();
CtValue MakeCtBool(bool value);
CtValue MakeCtInt(unsigned long long value,
                  std::string_view suffix,
                  std::string_view lexeme = {});
CtValue MakeCtFloat(std::string_view lexeme);
CtValue MakeCtChar(std::string_view lexeme);
const CtPrim* AsCtPrim(const CtValue& value);
bool TryGetCtBool(const CtValue& value, bool& out);
const CtPrimInt* TryGetCtInt(const CtValue& value);
bool CtIterable(const CtValue& value);
const std::vector<CtValue>* CtElems(const CtValue& value);

ExprPtr LiteralizeValue(const CtValue& value, const core::Span& span);
CtValue MakeSpanValue(const core::Span& span);

std::optional<CtAst> ParseQuotedAst(const QuoteExpr& quote,
                                    CtEnv& env,
                                    core::DiagnosticStream& diags,
                                    std::optional<ast::QuoteKind> expected_kind = std::nullopt);

CtValue MakeIoErrorValue(std::string_view variant);
std::shared_ptr<ProjectFileSnapshot> CaptureProjectFileSnapshot(
    const std::filesystem::path& project_root);
std::optional<EvalResult> EvalIntrospectMethod(const ast::MethodCallExpr& call,
                                               CtEnv& env);
std::optional<EvalResult> EvalProjectFilesMethod(const ast::MethodCallExpr& call,
                                                 CtEnv& env);

core::Span SpanOfItem(const ASTItem& item);

EvalResult EvalExpr(const ExprPtr& expr, CtEnv& env);
EvalResult EvalBlock(const Block& block, CtEnv& env);

std::optional<std::vector<ASTItem>> ExpandDerives(const ASTItem& item, CtEnv& env);
bool IsDeriveAnnotatedItem(const ASTItem& item);
std::optional<std::vector<ASTItem>> ExpandModuleItems(
    const std::vector<ASTItem>& items, CtEnv& env);

}  // namespace cursive::frontend::comptime_internal
