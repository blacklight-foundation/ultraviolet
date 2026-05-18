// ===========================================================================
// ast_nodes.cpp - AST Node Utility Functions
// ===========================================================================
//
// PURPOSE:
//   AST node utility functions for span extraction, node kind inspection,
//   and category checks. These utilities support error reporting, debugging,
//   and AST traversal operations.
//
// ===========================================================================
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.2 (Lines 2620-2639)
// ===========================================================================
//
//   SpanOfNode : ASTNode -> Span                           (line 2623)
//   DocOf : ASTNode -> (DocList | bottom)                  (line 2624)
//   SpanDefault(P, P') = SpanBetween(P, P')                (line 2626)
//   FillSpan(n, P, P') = n[span := SpanDefault(P, P')]     (line 2629-2631)
//   ParseCtor(n, P, P') = FillDocOpt(FillDoc(FillSpan(...))) (line 2638)
//
// ===========================================================================

#include "02_source/ast/ast.h"

#include <set>
#include <string>
#include <variant>

namespace ultraviolet::ast {

// ===========================================================================
// SPAN EXTRACTION
// ===========================================================================
// SpanOfNode : ASTNode -> Span
// Extracts the source span from any AST node type.

core::Span span_of(const Expr& e) {
  return e.span;
}

core::Span span_of(const Type& t) {
  return t.span;
}

core::Span span_of(const Pattern& p) {
  return p.span;
}

core::Span span_of(const Block& b) {
  return b.span;
}

core::Span span_of(const Stmt& s) {
  return std::visit(
      [](const auto& stmt) -> core::Span { return stmt.span; }, s);
}

core::Span span_of(const ASTItem& item) {
  return std::visit(
      [](const auto& decl) -> core::Span { return decl.span; }, item);
}

// ===========================================================================
// DOCUMENTATION EXTRACTION
// ===========================================================================
// DocOf : ASTNode -> (DocList | bottom)
// Extracts documentation from AST items that have doc fields.

const DocList* doc_of(const Expr& e) {
  (void)e;
  return nullptr;
}

const DocList* doc_of(const Type& t) {
  (void)t;
  return nullptr;
}

const DocList* doc_of(const Pattern& p) {
  (void)p;
  return nullptr;
}

const DocList* doc_of(const Stmt& s) {
  (void)s;
  return nullptr;
}

const DocList* doc_of(const ASTItem& item) {
  return std::visit(
      [](const auto& decl) -> const DocList* {
        return &decl.doc;
      },
      item);
}

// ===========================================================================
// EXPRESSION NODE KIND STRINGS
// ===========================================================================
// Returns a human-readable string for the expression node type.
// Used for debugging and error messages.

const char* node_kind(const Expr& e) {
  return std::visit(
      [](const auto& node) -> const char* {
        using T = std::decay_t<decltype(node)>;
        // Error/Basic
        if constexpr (std::is_same_v<T, ErrorExpr>) return "ErrorExpr";
        if constexpr (std::is_same_v<T, LiteralExpr>) return "LiteralExpr";
        if constexpr (std::is_same_v<T, IdentifierExpr>) return "IdentifierExpr";
        if constexpr (std::is_same_v<T, QualifiedNameExpr>) return "QualifiedNameExpr";
        // Qualified
        if constexpr (std::is_same_v<T, QualifiedApplyExpr>) return "QualifiedApplyExpr";
        if constexpr (std::is_same_v<T, PathExpr>) return "PathExpr";
        // Operators
        if constexpr (std::is_same_v<T, RangeExpr>) return "RangeExpr";
        if constexpr (std::is_same_v<T, BinaryExpr>) return "BinaryExpr";
        if constexpr (std::is_same_v<T, CastExpr>) return "CastExpr";
        if constexpr (std::is_same_v<T, UnaryExpr>) return "UnaryExpr";
        // Memory
        if constexpr (std::is_same_v<T, DerefExpr>) return "DerefExpr";
        if constexpr (std::is_same_v<T, AddressOfExpr>) return "AddressOfExpr";
        if constexpr (std::is_same_v<T, MoveExpr>) return "MoveExpr";
        if constexpr (std::is_same_v<T, CopyExpr>) return "CopyExpr";
        if constexpr (std::is_same_v<T, AllocExpr>) return "AllocExpr";
        if constexpr (std::is_same_v<T, PtrNullExpr>) return "PtrNullExpr";
        // Aggregate
        if constexpr (std::is_same_v<T, TupleExpr>) return "TupleExpr";
        if constexpr (std::is_same_v<T, ArrayExpr>) return "ArrayExpr";
        if constexpr (std::is_same_v<T, ArrayRepeatExpr>) return "ArrayRepeatExpr";
        if constexpr (std::is_same_v<T, SizeofExpr>) return "SizeofExpr";
        if constexpr (std::is_same_v<T, AlignofExpr>) return "AlignofExpr";
        if constexpr (std::is_same_v<T, RecordExpr>) return "RecordExpr";
        if constexpr (std::is_same_v<T, EnumLiteralExpr>) return "EnumLiteralExpr";
        if constexpr (std::is_same_v<T, TypeLiteralExpr>) return "TypeLiteralExpr";
        if constexpr (std::is_same_v<T, QuoteExpr>) return "QuoteExpr";
        // Control flow
        if constexpr (std::is_same_v<T, IfExpr>) return "IfExpr";
        if constexpr (std::is_same_v<T, IfIsExpr>) return "IfIsExpr";
        if constexpr (std::is_same_v<T, IfCaseExpr>) return "IfCaseExpr";
        if constexpr (std::is_same_v<T, LoopInfiniteExpr>) return "LoopInfiniteExpr";
        if constexpr (std::is_same_v<T, LoopConditionalExpr>) return "LoopConditionalExpr";
        if constexpr (std::is_same_v<T, LoopIterExpr>) return "LoopIterExpr";
        if constexpr (std::is_same_v<T, BlockExpr>) return "BlockExpr";
        if constexpr (std::is_same_v<T, UnsafeBlockExpr>) return "UnsafeBlockExpr";
        if constexpr (std::is_same_v<T, ComptimeExpr>) return "ComptimeExpr";
        if constexpr (std::is_same_v<T, CtIfExpr>) return "CtIfExpr";
        if constexpr (std::is_same_v<T, CtLoopIterExpr>) return "CtLoopIterExpr";
        // Special
        if constexpr (std::is_same_v<T, AttributedExpr>) return "AttributedExpr";
        if constexpr (std::is_same_v<T, TransmuteExpr>) return "TransmuteExpr";
        // Postfix
        if constexpr (std::is_same_v<T, FieldAccessExpr>) return "FieldAccessExpr";
        if constexpr (std::is_same_v<T, TupleAccessExpr>) return "TupleAccessExpr";
        if constexpr (std::is_same_v<T, IndexAccessExpr>) return "IndexAccessExpr";
        if constexpr (std::is_same_v<T, CallExpr>) return "CallExpr";
        if constexpr (std::is_same_v<T, CallTypeArgsExpr>) return "CallTypeArgsExpr";
        if constexpr (std::is_same_v<T, MethodCallExpr>) return "MethodCallExpr";
        if constexpr (std::is_same_v<T, PropagateExpr>) return "PropagateExpr";
        // Contract
        if constexpr (std::is_same_v<T, ResultExpr>) return "ResultExpr";
        if constexpr (std::is_same_v<T, EntryExpr>) return "EntryExpr";
        // Async
        if constexpr (std::is_same_v<T, YieldExpr>) return "YieldExpr";
        if constexpr (std::is_same_v<T, YieldFromExpr>) return "YieldFromExpr";
        if constexpr (std::is_same_v<T, SyncExpr>) return "SyncExpr";
        if constexpr (std::is_same_v<T, RaceExpr>) return "RaceExpr";
        if constexpr (std::is_same_v<T, AllExpr>) return "AllExpr";
        // Concurrency
        if constexpr (std::is_same_v<T, ParallelExpr>) return "ParallelExpr";
        if constexpr (std::is_same_v<T, SpawnExpr>) return "SpawnExpr";
        if constexpr (std::is_same_v<T, WaitExpr>) return "WaitExpr";
        if constexpr (std::is_same_v<T, FenceExpr>) return "FenceExpr";
        if constexpr (std::is_same_v<T, DispatchExpr>) return "DispatchExpr";
        return "UnknownExpr";
      },
      e.node);
}

// ===========================================================================
// TYPE NODE KIND STRINGS
// ===========================================================================

const char* node_kind(const Type& t) {
  return std::visit(
      [](const auto& node) -> const char* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePrim>) return "TypePrim";
        if constexpr (std::is_same_v<T, TypePermType>) return "TypePermType";
        if constexpr (std::is_same_v<T, TypeUnion>) return "TypeUnion";
        if constexpr (std::is_same_v<T, TypeFunc>) return "TypeFunc";
        if constexpr (std::is_same_v<T, TypeClosure>) return "TypeClosure";
        if constexpr (std::is_same_v<T, TypeTuple>) return "TypeTuple";
        if constexpr (std::is_same_v<T, TypeArray>) return "TypeArray";
        if constexpr (std::is_same_v<T, TypeSlice>) return "TypeSlice";
        if constexpr (std::is_same_v<T, TypeSafePtr>) return "TypeSafePtr";
        if constexpr (std::is_same_v<T, TypeRawPtr>) return "TypeRawPtr";
        if constexpr (std::is_same_v<T, TypeString>) return "TypeString";
        if constexpr (std::is_same_v<T, TypeBytes>) return "TypeBytes";
        if constexpr (std::is_same_v<T, TypeDynamic>) return "TypeDynamic";
        if constexpr (std::is_same_v<T, TypeModalState>) return "TypeModalState";
        if constexpr (std::is_same_v<T, TypePathType>) return "TypePathType";
        if constexpr (std::is_same_v<T, TypeApply>) return "TypeApply";
        if constexpr (std::is_same_v<T, TypeOpaque>) return "TypeOpaque";
        if constexpr (std::is_same_v<T, TypeRefine>) return "TypeRefine";
        if constexpr (std::is_same_v<T, TypeRange>) return "TypeRange";
        if constexpr (std::is_same_v<T, TypeRangeInclusive>) return "TypeRangeInclusive";
        if constexpr (std::is_same_v<T, TypeRangeFrom>) return "TypeRangeFrom";
        if constexpr (std::is_same_v<T, TypeRangeTo>) return "TypeRangeTo";
        if constexpr (std::is_same_v<T, TypeRangeToInclusive>) return "TypeRangeToInclusive";
        if constexpr (std::is_same_v<T, TypeRangeFull>) return "TypeRangeFull";
        return "UnknownType";
      },
      t.node);
}

// ===========================================================================
// PATTERN NODE KIND STRINGS
// ===========================================================================

const char* node_kind(const Pattern& p) {
  return std::visit(
      [](const auto& node) -> const char* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralPattern>) return "LiteralPattern";
        if constexpr (std::is_same_v<T, WildcardPattern>) return "WildcardPattern";
        if constexpr (std::is_same_v<T, IdentifierPattern>) return "IdentifierPattern";
        if constexpr (std::is_same_v<T, TypedPattern>) return "TypedPattern";
        if constexpr (std::is_same_v<T, TuplePattern>) return "TuplePattern";
        if constexpr (std::is_same_v<T, RecordPattern>) return "RecordPattern";
        if constexpr (std::is_same_v<T, EnumPattern>) return "EnumPattern";
        if constexpr (std::is_same_v<T, ModalPattern>) return "ModalPattern";
        if constexpr (std::is_same_v<T, RangePattern>) return "RangePattern";
        return "UnknownPattern";
      },
      p.node);
}

// ===========================================================================
// STATEMENT NODE KIND STRINGS
// ===========================================================================

const char* node_kind(const Stmt& s) {
  return std::visit(
      [](const auto& node) -> const char* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LetStmt>) return "LetStmt";
        if constexpr (std::is_same_v<T, VarStmt>) return "VarStmt";
        if constexpr (std::is_same_v<T, UsingLocalStmt>) return "UsingLocalStmt";
        if constexpr (std::is_same_v<T, AssignStmt>) return "AssignStmt";
        if constexpr (std::is_same_v<T, CompoundAssignStmt>) return "CompoundAssignStmt";
        if constexpr (std::is_same_v<T, ExprStmt>) return "ExprStmt";
        if constexpr (std::is_same_v<T, DeferStmt>) return "DeferStmt";
        if constexpr (std::is_same_v<T, RegionStmt>) return "RegionStmt";
        if constexpr (std::is_same_v<T, FrameStmt>) return "FrameStmt";
        if constexpr (std::is_same_v<T, ReturnStmt>) return "ReturnStmt";
        if constexpr (std::is_same_v<T, BreakStmt>) return "BreakStmt";
        if constexpr (std::is_same_v<T, ContinueStmt>) return "ContinueStmt";
        if constexpr (std::is_same_v<T, UnsafeBlockStmt>) return "UnsafeBlockStmt";
        if constexpr (std::is_same_v<T, CtStmt>) return "CtStmt";
        if constexpr (std::is_same_v<T, KeyBlockStmt>) return "KeyBlockStmt";
        if constexpr (std::is_same_v<T, ErrorStmt>) return "ErrorStmt";
        return "UnknownStmt";
      },
      s);
}

// ===========================================================================
// ITEM NODE KIND STRINGS
// ===========================================================================

const char* node_kind(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> const char* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UsingDecl>) return "UsingDecl";
        if constexpr (std::is_same_v<T, ImportDecl>) return "ImportDecl";
        if constexpr (std::is_same_v<T, ExternBlock>) return "ExternBlock";
        if constexpr (std::is_same_v<T, StaticDecl>) return "StaticDecl";
        if constexpr (std::is_same_v<T, ProcedureDecl>) return "ProcedureDecl";
        if constexpr (std::is_same_v<T, ComptimeProcedureDecl>) return "ComptimeProcedureDecl";
        if constexpr (std::is_same_v<T, RecordDecl>) return "RecordDecl";
        if constexpr (std::is_same_v<T, EnumDecl>) return "EnumDecl";
        if constexpr (std::is_same_v<T, ModalDecl>) return "ModalDecl";
        if constexpr (std::is_same_v<T, ClassDecl>) return "ClassDecl";
        if constexpr (std::is_same_v<T, TypeAliasDecl>) return "TypeAliasDecl";
        if constexpr (std::is_same_v<T, DeriveTargetDecl>) return "DeriveTargetDecl";
        if constexpr (std::is_same_v<T, ErrorItem>) return "ErrorItem";
        return "UnknownItem";
      },
      item);
}

// ===========================================================================
// EXPRESSION CATEGORY CHECKS
// ===========================================================================
// These predicates classify expressions by their semantic category.

bool is_literal(const ExprNode& n) {
  return std::holds_alternative<LiteralExpr>(n);
}

bool is_binary_op(const ExprNode& n) {
  return std::holds_alternative<BinaryExpr>(n);
}

bool is_control_flow(const ExprNode& n) {
  return std::holds_alternative<IfExpr>(n) ||
         std::holds_alternative<IfIsExpr>(n) ||
         std::holds_alternative<IfCaseExpr>(n) ||
         std::holds_alternative<LoopInfiniteExpr>(n) ||
         std::holds_alternative<LoopConditionalExpr>(n) ||
         std::holds_alternative<LoopIterExpr>(n) ||
         std::holds_alternative<BlockExpr>(n) ||
         std::holds_alternative<ComptimeExpr>(n) ||
         std::holds_alternative<CtIfExpr>(n) ||
         std::holds_alternative<CtLoopIterExpr>(n);
}

bool is_memory_op(const ExprNode& n) {
  return std::holds_alternative<DerefExpr>(n) ||
         std::holds_alternative<AddressOfExpr>(n) ||
         std::holds_alternative<MoveExpr>(n) ||
         std::holds_alternative<AllocExpr>(n);
}

bool is_async(const ExprNode& n) {
  return std::holds_alternative<YieldExpr>(n) ||
         std::holds_alternative<YieldFromExpr>(n) ||
         std::holds_alternative<SyncExpr>(n) ||
         std::holds_alternative<RaceExpr>(n) ||
         std::holds_alternative<AllExpr>(n);
}

bool is_concurrency(const ExprNode& n) {
  return std::holds_alternative<ParallelExpr>(n) ||
         std::holds_alternative<SpawnExpr>(n) ||
         std::holds_alternative<WaitExpr>(n) ||
         std::holds_alternative<FenceExpr>(n) ||
         std::holds_alternative<DispatchExpr>(n);
}

// ===========================================================================
// PATTERN CATEGORY CHECKS
// ===========================================================================
// SPEC: Irrefutable patterns always match; refutable patterns may fail.
//
// Irrefutable patterns:
//   - WildcardPattern (_)
//   - IdentifierPattern (x)
//   - TypedPattern (x: T) - binds a name unconditionally
//   - TuplePattern with all irrefutable elements
//   - RecordPattern with all irrefutable fields
//
// Refutable patterns:
//   - LiteralPattern (42, "hello")
//   - RangePattern (0..10)
//   - EnumPattern (Result::Ok(v)) - may fail if wrong variant
//   - ModalPattern (@State{...}) - may fail if wrong state

bool is_irrefutable(const PatternNode& p);

namespace {

bool is_pattern_irrefutable_impl(const PatternNode& p) {
  return std::visit(
      [](const auto& node) -> bool {
        using T = std::decay_t<decltype(node)>;
        // Always irrefutable: wildcard and simple identifier binding
        if constexpr (std::is_same_v<T, WildcardPattern>) {
          return true;
        }
        if constexpr (std::is_same_v<T, IdentifierPattern>) {
          return true;
        }
        if constexpr (std::is_same_v<T, TypedPattern>) {
          // Typed pattern (x: T) is irrefutable - it always binds
          return true;
        }
        // Tuple pattern is irrefutable if all elements are irrefutable
        if constexpr (std::is_same_v<T, TuplePattern>) {
          for (const auto& elem : node.elements) {
            if (!is_irrefutable(elem->node)) {
              return false;
            }
          }
          return true;
        }
        // Record pattern is irrefutable if all fields are irrefutable
        if constexpr (std::is_same_v<T, RecordPattern>) {
          for (const auto& field : node.fields) {
            if (field.pattern_opt && !is_irrefutable(field.pattern_opt->node)) {
              return false;
            }
          }
          return true;
        }
        // All other patterns are refutable
        return false;
      },
      p);
}

}  // namespace

bool is_irrefutable(const PatternNode& p) {
  return is_pattern_irrefutable_impl(p);
}

bool is_refutable(const PatternNode& p) {
  return !is_irrefutable(p);
}

// ===========================================================================
// STATEMENT CATEGORY CHECKS
// ===========================================================================

bool is_binding(const Stmt& s) {
  return std::holds_alternative<LetStmt>(s) ||
         std::holds_alternative<VarStmt>(s) ||
         std::holds_alternative<UsingLocalStmt>(s);
}

bool is_control(const Stmt& s) {
  return std::holds_alternative<ReturnStmt>(s) ||
         std::holds_alternative<BreakStmt>(s) ||
         std::holds_alternative<ContinueStmt>(s);
}

bool is_assignment(const Stmt& s) {
  return std::holds_alternative<AssignStmt>(s) ||
         std::holds_alternative<CompoundAssignStmt>(s);
}

bool is_resource_stmt(const Stmt& s) {
  return std::holds_alternative<RegionStmt>(s) ||
         std::holds_alternative<FrameStmt>(s) ||
         std::holds_alternative<DeferStmt>(s);
}

// ===========================================================================
// ITEM CATEGORY CHECKS
// ===========================================================================

bool is_declaration(const ASTItem& item) {
  // All items except ErrorItem are declarations
  return !std::holds_alternative<ErrorItem>(item);
}

bool is_type_definition(const ASTItem& item) {
  return std::holds_alternative<RecordDecl>(item) ||
         std::holds_alternative<EnumDecl>(item) ||
         std::holds_alternative<ModalDecl>(item) ||
         std::holds_alternative<ClassDecl>(item) ||
         std::holds_alternative<TypeAliasDecl>(item);
}

bool is_value_definition(const ASTItem& item) {
  return std::holds_alternative<StaticDecl>(item) ||
         std::holds_alternative<ProcedureDecl>(item) ||
         std::holds_alternative<ComptimeProcedureDecl>(item);
}

bool is_import_or_using(const ASTItem& item) {
  return std::holds_alternative<UsingDecl>(item) ||
         std::holds_alternative<ImportDecl>(item);
}

namespace {

std::string JoinPath(const TypePath& path) {
  if (path.empty()) {
    return "";
  }
  std::string out = path.front();
  for (std::size_t i = 1; i < path.size(); ++i) {
    out += "::";
    out += path[i];
  }
  return out;
}

void collect_stmt_nodes_from_block(const BlockPtr& block,
                                   std::vector<const Stmt*>& out) {
  if (!block) {
    return;
  }
  for (const auto& stmt : block->stmts) {
    out.push_back(&stmt);
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, DeferStmt> ||
                        std::is_same_v<T, RegionStmt> ||
                        std::is_same_v<T, FrameStmt> ||
                        std::is_same_v<T, CtStmt> ||
                        std::is_same_v<T, UnsafeBlockStmt> ||
                        std::is_same_v<T, KeyBlockStmt>) {
            collect_stmt_nodes_from_block(node.body, out);
          }
        },
        stmt);
  }
}

void collect_expr_nodes_from_expr(const ExprPtr& expr,
                                  std::vector<const Expr*>& out);

void collect_expr_nodes_from_stmt(const Stmt& stmt,
                                  std::vector<const Expr*>& out) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LetStmt> || std::is_same_v<T, VarStmt>) {
          collect_expr_nodes_from_expr(node.binding.init, out);
        } else if constexpr (std::is_same_v<T, UsingLocalStmt>) {
          // UsingLocalStmt has no expressions
          (void)node;
        } else if constexpr (std::is_same_v<T, AssignStmt>) {
          collect_expr_nodes_from_expr(node.place, out);
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, CompoundAssignStmt>) {
          collect_expr_nodes_from_expr(node.place, out);
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, ExprStmt>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          collect_expr_nodes_from_expr(node.value_opt, out);
        } else if constexpr (std::is_same_v<T, DeferStmt> ||
                             std::is_same_v<T, RegionStmt> ||
                             std::is_same_v<T, FrameStmt> ||
                             std::is_same_v<T, CtStmt> ||
                             std::is_same_v<T, UnsafeBlockStmt> ||
                             std::is_same_v<T, KeyBlockStmt>) {
          if (node.body) {
            for (const auto& inner : node.body->stmts) {
              collect_expr_nodes_from_stmt(inner, out);
            }
            collect_expr_nodes_from_expr(node.body->tail_opt, out);
          }
        }
      },
      stmt);
}

void collect_expr_nodes_from_block(const BlockPtr& block,
                                   std::vector<const Expr*>& out) {
  if (!block) {
    return;
  }
  for (const auto& stmt : block->stmts) {
    collect_expr_nodes_from_stmt(stmt, out);
  }
  collect_expr_nodes_from_expr(block->tail_opt, out);
}

static void collect_parallel_option_expr_nodes(
    const std::vector<ParallelOption>& opts,
    std::vector<const Expr*>& out) {
  for (const auto& opt : opts) {
    switch (opt.kind) {
      case ParallelOptionKind::Cancel:
      case ParallelOptionKind::Workgroup:
      case ParallelOptionKind::Workgroups:
        collect_expr_nodes_from_expr(opt.value, out);
        break;
      case ParallelOptionKind::Name:
        break;
    }
  }
}

static void collect_dispatch_option_expr_nodes(
    const std::vector<DispatchOption>& opts,
    std::vector<const Expr*>& out) {
  for (const auto& opt : opts) {
    switch (opt.kind) {
      case DispatchOptionKind::Chunk:
        collect_expr_nodes_from_expr(opt.chunk_expr, out);
        break;
      case DispatchOptionKind::Workgroup:
        collect_expr_nodes_from_expr(opt.workgroup_expr, out);
        break;
      case DispatchOptionKind::Reduce:
      case DispatchOptionKind::Ordered:
        break;
    }
  }
}

static void collect_spawn_option_expr_nodes(
    const std::vector<SpawnOption>& opts,
    std::vector<const Expr*>& out) {
  for (const auto& opt : opts) {
    switch (opt.kind) {
      case SpawnOptionKind::Affinity:
      case SpawnOptionKind::Priority:
        collect_expr_nodes_from_expr(opt.value, out);
        break;
      case SpawnOptionKind::Name:
        break;
    }
  }
}

void collect_expr_nodes_from_expr(const ExprPtr& expr,
                                  std::vector<const Expr*>& out) {
  if (!expr) {
    return;
  }
  out.push_back(expr.get());
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, BinaryExpr>) {
          collect_expr_nodes_from_expr(node.lhs, out);
          collect_expr_nodes_from_expr(node.rhs, out);
        } else if constexpr (std::is_same_v<T, CastExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, DerefExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, AddressOfExpr>) {
          collect_expr_nodes_from_expr(node.place, out);
        } else if constexpr (std::is_same_v<T, MoveExpr>) {
          collect_expr_nodes_from_expr(node.place, out);
        } else if constexpr (std::is_same_v<T, AllocExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, TupleExpr>) {
          for (const auto& e : node.elements) {
            collect_expr_nodes_from_expr(e, out);
          }
        } else if constexpr (std::is_same_v<T, ArrayExpr>) {
          ForEachArrayExprSubexpr(node, [&](const auto& e) {
            collect_expr_nodes_from_expr(e, out);
          });
        } else if constexpr (std::is_same_v<T, ArrayRepeatExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
          collect_expr_nodes_from_expr(node.count, out);
        } else if constexpr (std::is_same_v<T, RecordExpr>) {
          for (const auto& field : node.fields) {
            collect_expr_nodes_from_expr(field.value, out);
          }
        } else if constexpr (std::is_same_v<T, EnumLiteralExpr>) {
          if (node.payload_opt.has_value()) {
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, EnumPayloadParen>) {
                    for (const auto& e : payload.elements) {
                      collect_expr_nodes_from_expr(e, out);
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      collect_expr_nodes_from_expr(field.value, out);
                    }
                  }
                },
                *node.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, IfExpr>) {
          collect_expr_nodes_from_expr(node.cond, out);
          collect_expr_nodes_from_expr(node.then_expr, out);
          collect_expr_nodes_from_expr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, IfIsExpr>) {
          collect_expr_nodes_from_expr(node.scrutinee, out);
          collect_expr_nodes_from_expr(node.then_expr, out);
          collect_expr_nodes_from_expr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, IfCaseExpr>) {
          collect_expr_nodes_from_expr(node.scrutinee, out);
          for (const auto& arm : node.cases) {
            collect_expr_nodes_from_expr(arm.body, out);
          }
          collect_expr_nodes_from_expr(node.else_expr, out);
        } else if constexpr (std::is_same_v<T, LoopInfiniteExpr>) {
          if (node.invariant_opt.has_value()) {
            collect_expr_nodes_from_expr(node.invariant_opt->predicate, out);
          }
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, LoopConditionalExpr>) {
          collect_expr_nodes_from_expr(node.cond, out);
          if (node.invariant_opt.has_value()) {
            collect_expr_nodes_from_expr(node.invariant_opt->predicate, out);
          }
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, LoopIterExpr>) {
          collect_expr_nodes_from_expr(node.iter, out);
          if (node.invariant_opt.has_value()) {
            collect_expr_nodes_from_expr(node.invariant_opt->predicate, out);
          }
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, BlockExpr>) {
          collect_expr_nodes_from_block(node.block, out);
        } else if constexpr (std::is_same_v<T, UnsafeBlockExpr>) {
          collect_expr_nodes_from_block(node.block, out);
        } else if constexpr (std::is_same_v<T, ComptimeExpr>) {
          collect_expr_nodes_from_expr(node.body, out);
        } else if constexpr (std::is_same_v<T, CtIfExpr>) {
          collect_expr_nodes_from_expr(node.cond, out);
          collect_expr_nodes_from_block(node.then_block, out);
          collect_expr_nodes_from_block(node.else_block_opt, out);
        } else if constexpr (std::is_same_v<T, CtLoopIterExpr>) {
          collect_expr_nodes_from_expr(node.iter, out);
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, AttributedExpr>) {
          collect_expr_nodes_from_expr(node.expr, out);
        } else if constexpr (std::is_same_v<T, TransmuteExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, ClosureExpr>) {
          collect_expr_nodes_from_expr(node.body, out);
        } else if constexpr (std::is_same_v<T, PipelineExpr>) {
          collect_expr_nodes_from_expr(node.lhs, out);
          collect_expr_nodes_from_expr(node.rhs, out);
        } else if constexpr (std::is_same_v<T, FieldAccessExpr>) {
          collect_expr_nodes_from_expr(node.base, out);
        } else if constexpr (std::is_same_v<T, TupleAccessExpr>) {
          collect_expr_nodes_from_expr(node.base, out);
        } else if constexpr (std::is_same_v<T, IndexAccessExpr>) {
          collect_expr_nodes_from_expr(node.base, out);
          collect_expr_nodes_from_expr(node.index, out);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          collect_expr_nodes_from_expr(node.callee, out);
          for (const auto& arg : node.args) {
            collect_expr_nodes_from_expr(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, CallTypeArgsExpr>) {
          collect_expr_nodes_from_expr(node.callee, out);
          for (const auto& arg : node.args) {
            collect_expr_nodes_from_expr(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, MethodCallExpr>) {
          collect_expr_nodes_from_expr(node.receiver, out);
          for (const auto& arg : node.args) {
            collect_expr_nodes_from_expr(arg.value, out);
          }
        } else if constexpr (std::is_same_v<T, PropagateExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, EntryExpr>) {
          collect_expr_nodes_from_expr(node.expr, out);
        } else if constexpr (std::is_same_v<T, YieldExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, YieldFromExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, SyncExpr>) {
          collect_expr_nodes_from_expr(node.value, out);
        } else if constexpr (std::is_same_v<T, RaceExpr>) {
          for (const auto& arm : node.arms) {
            collect_expr_nodes_from_expr(arm.expr, out);
            collect_expr_nodes_from_expr(arm.handler.value, out);
          }
        } else if constexpr (std::is_same_v<T, AllExpr>) {
          for (const auto& e : node.exprs) {
            collect_expr_nodes_from_expr(e, out);
          }
        } else if constexpr (std::is_same_v<T, ParallelExpr>) {
          collect_expr_nodes_from_expr(node.domain, out);
          collect_parallel_option_expr_nodes(node.opts, out);
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, SpawnExpr>) {
          collect_spawn_option_expr_nodes(node.opts, out);
          collect_expr_nodes_from_block(node.body, out);
        } else if constexpr (std::is_same_v<T, WaitExpr>) {
          collect_expr_nodes_from_expr(node.handle, out);
        } else if constexpr (std::is_same_v<T, DispatchExpr>) {
          collect_expr_nodes_from_expr(node.range, out);
          collect_dispatch_option_expr_nodes(node.opts, out);
          collect_expr_nodes_from_block(node.body, out);
          if (node.key_clause.has_value()) {
            for (const auto& seg : node.key_clause->key_path.segs) {
              if (const auto* idx = std::get_if<KeySegIndex>(&seg)) {
                collect_expr_nodes_from_expr(idx->expr, out);
              }
            }
          }
        }
      },
      expr->node);
}

void collect_type_nodes_from_item(const ASTItem& item,
                                  std::vector<const Type*>& out) {
  auto push_type = [&](const TypePtr& t) {
    if (t) {
      out.push_back(t.get());
    }
  };
  auto push_params = [&](const std::vector<Param>& params) {
    for (const auto& param : params) {
      push_type(param.type);
    }
  };

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, StaticDecl>) {
          push_type(BindingAnnotationTypeOpt(node.binding));
        } else if constexpr (std::is_same_v<T, ProcedureDecl>) {
          push_params(node.params);
          push_type(node.return_type_opt);
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
        } else if constexpr (std::is_same_v<T, ComptimeProcedureDecl>) {
          push_params(node.params);
          push_type(node.return_type_opt);
        } else if constexpr (std::is_same_v<T, ExternBlock>) {
          for (const auto& ext : node.items) {
            std::visit(
                [&](const auto& ext_item) {
                  push_params(ext_item.params);
                  push_type(ext_item.return_type_opt);
                  if (ext_item.where_clause.has_value()) {
                    for (const auto& pred : *ext_item.where_clause) {
                      push_type(pred.type);
                    }
                  }
                },
                ext);
          }
        } else if constexpr (std::is_same_v<T, RecordDecl>) {
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
          for (const auto& member : node.members) {
            std::visit(
                [&](const auto& m) {
                  using M = std::decay_t<decltype(m)>;
                  if constexpr (std::is_same_v<M, FieldDecl>) {
                    push_type(m.type);
                  } else if constexpr (std::is_same_v<M, MethodDecl>) {
                    if (const auto* explicit_recv =
                            std::get_if<ReceiverExplicit>(&m.receiver)) {
                      push_type(explicit_recv->type);
                    }
                    push_params(m.params);
                    push_type(m.return_type_opt);
                  } else if constexpr (std::is_same_v<M, AssociatedTypeDecl>) {
                    push_type(m.default_type);
                  }
                },
                member);
          }
        } else if constexpr (std::is_same_v<T, EnumDecl>) {
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
          for (const auto& variant : node.variants) {
            if (!variant.payload_opt.has_value()) {
              continue;
            }
            std::visit(
                [&](const auto& payload) {
                  using P = std::decay_t<decltype(payload)>;
                  if constexpr (std::is_same_v<P, VariantPayloadTuple>) {
                    for (const auto& elem : payload.elements) {
                      push_type(elem);
                    }
                  } else {
                    for (const auto& field : payload.fields) {
                      push_type(field.type);
                    }
                  }
                },
                *variant.payload_opt);
          }
        } else if constexpr (std::is_same_v<T, ModalDecl>) {
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
          for (const auto& state : node.states) {
            for (const auto& member : state.members) {
              std::visit(
                  [&](const auto& m) {
                    using M = std::decay_t<decltype(m)>;
                    if constexpr (std::is_same_v<M, StateFieldDecl>) {
                      push_type(m.type);
                    } else if constexpr (std::is_same_v<M, StateMethodDecl>) {
                      if (const auto* explicit_recv =
                              std::get_if<ReceiverExplicit>(&m.receiver)) {
                        push_type(explicit_recv->type);
                      }
                      push_params(m.params);
                      push_type(m.return_type_opt);
                    } else if constexpr (std::is_same_v<M, TransitionDecl>) {
                      push_params(m.params);
                    }
                  },
                  member);
            }
          }
        } else if constexpr (std::is_same_v<T, ClassDecl>) {
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
          for (const auto& class_item : node.items) {
            std::visit(
                [&](const auto& m) {
                  using M = std::decay_t<decltype(m)>;
                  if constexpr (std::is_same_v<M, ClassFieldDecl> ||
                                std::is_same_v<M, AbstractFieldDecl>) {
                    push_type(m.type);
                  } else if constexpr (std::is_same_v<M, ClassMethodDecl>) {
                    if (const auto* explicit_recv =
                            std::get_if<ReceiverExplicit>(&m.receiver)) {
                      push_type(explicit_recv->type);
                    }
                    push_params(m.params);
                    push_type(m.return_type_opt);
                  } else if constexpr (std::is_same_v<M, AssociatedTypeDecl>) {
                    push_type(m.default_type);
                  } else if constexpr (std::is_same_v<M, AbstractStateDecl>) {
                    for (const auto& field : m.fields) {
                      push_type(field.type);
                    }
                  }
                },
                class_item);
          }
        } else if constexpr (std::is_same_v<T, TypeAliasDecl>) {
          push_type(node.type);
          if (node.predicate_clause_opt.has_value()) {
            for (const auto& pred : *node.predicate_clause_opt) {
              push_type(pred.type);
            }
          }
        }
      },
      item);
}

std::string TypeCtorTag(const Type& t) {
  return std::visit(
      [&](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePermType>) return "TypePerm(_, base)";
        if constexpr (std::is_same_v<T, TypePrim>) return "TypePrim(name)";
        if constexpr (std::is_same_v<T, TypeTuple>) return "TypeTuple(elems)";
        if constexpr (std::is_same_v<T, TypeArray>) return "TypeArray(elem, _)";
        if constexpr (std::is_same_v<T, TypeSlice>) return "TypeSlice(elem)";
        if constexpr (std::is_same_v<T, TypeUnion>) return "TypeUnion(members)";
        if constexpr (std::is_same_v<T, TypeFunc>) return "TypeFunc(params, ret)";
        if constexpr (std::is_same_v<T, TypeApply>) return "TypeApply(path, _)";
        if constexpr (std::is_same_v<T, TypeSafePtr>) return "TypePtr(elem, _)";
        if constexpr (std::is_same_v<T, TypeRawPtr>) return "TypeRawPtr(_, elem)";
        if constexpr (std::is_same_v<T, TypeString>) return "TypeString(_)";
        if constexpr (std::is_same_v<T, TypeBytes>) return "TypeBytes(_)";
        if constexpr (std::is_same_v<T, TypeDynamic>) return "TypeDynamic(_)";
        if constexpr (std::is_same_v<T, TypeOpaque>) return "TypeOpaque(_)";
        if constexpr (std::is_same_v<T, TypeRefine>) return "TypeRefine(base, _)";
        if constexpr (std::is_same_v<T, TypeModalState>) return "TypeModalState(_, _)";
        if constexpr (std::is_same_v<T, TypeRange>) return "TypeRange(base)";
        if constexpr (std::is_same_v<T, TypeRangeInclusive>) return "TypeRangeInclusive(base)";
        if constexpr (std::is_same_v<T, TypeRangeFrom>) return "TypeRangeFrom(base)";
        if constexpr (std::is_same_v<T, TypeRangeTo>) return "TypeRangeTo(base)";
        if constexpr (std::is_same_v<T, TypeRangeToInclusive>) return "TypeRangeToInclusive(base)";
        if constexpr (std::is_same_v<T, TypeRangeFull>) return "TypeRangeFull";
        if constexpr (std::is_same_v<T, TypeClosure>) return "TypeClosure(params, ret, _)";
        if constexpr (std::is_same_v<T, TypePathType>) {
          if (node.path.size() == 1 && node.path.front() == "Region") {
            return "TypePath([\"Region\"])";
          }
          if (node.path.size() == 1 && node.path.front() == "RegionOptions") {
            return "TypePath([\"RegionOptions\"])";
          }
          return "TypePath(p)";
        }
        return "TypeCtor(_)";
      },
      t.node);
}

std::string ItemKindTag(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, UsingDecl>) return "using_decl";
        if constexpr (std::is_same_v<T, ProcedureDecl>) return "procedure";
        if constexpr (std::is_same_v<T, RecordDecl>) return "record";
        if constexpr (std::is_same_v<T, EnumDecl>) return "enum";
        if constexpr (std::is_same_v<T, ModalDecl>) return "modal";
        if constexpr (std::is_same_v<T, ClassDecl>) return "class";
        if constexpr (std::is_same_v<T, TypeAliasDecl>) return "type_alias";
        if constexpr (std::is_same_v<T, StaticDecl>) return "static_decl";
        return "";
      },
      item);
}

std::string AppendixItemFormTag(const ASTItem& item) {
  return std::visit(
      [](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ComptimeProcedureDecl>) return "CtProc";
        if constexpr (std::is_same_v<T, CtStmt>) return "CtStmt";
        if constexpr (std::is_same_v<T, TypeAliasDecl>) return "TypeAliasDecl";
        if constexpr (std::is_same_v<T, UsingDecl>) return "UsingDecl";
        if constexpr (std::is_same_v<T, ImportDecl>) return "ImportDecl";
        if constexpr (std::is_same_v<T, ExternBlock>) return "ExternBlock";
        if constexpr (std::is_same_v<T, StaticDecl>) return "StaticDecl";
        if constexpr (std::is_same_v<T, ProcedureDecl>) return "ProcedureDecl";
        if constexpr (std::is_same_v<T, DeriveTargetDecl>) return "DeriveTargetDecl";
        if constexpr (std::is_same_v<T, RecordDecl>) return "RecordDecl";
        if constexpr (std::is_same_v<T, EnumDecl>) return "EnumDecl";
        if constexpr (std::is_same_v<T, ModalDecl>) return "ModalDecl";
        if constexpr (std::is_same_v<T, ClassDecl>) return "ClassDecl";
        return "";
      },
      item);
}

std::string AppendixTypeFormTag(const Type& t) {
  return std::visit(
      [](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, TypePermType>) return "TypePerm";
        if constexpr (std::is_same_v<T, TypePathType>) return "TypePath";
        if constexpr (std::is_same_v<T, TypeApply>) return "TypeApply";
        if constexpr (std::is_same_v<T, TypeSafePtr>) return "TypePtr";
        return "";
      },
      t.node);
}

std::string StmtKindTag(const Stmt& s) {
  return std::visit(
      [](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LetStmt>) return "let";
        if constexpr (std::is_same_v<T, VarStmt>) return "var";
        if constexpr (std::is_same_v<T, UsingLocalStmt>) return "using";
        if constexpr (std::is_same_v<T, AssignStmt>) return "assign";
        if constexpr (std::is_same_v<T, CompoundAssignStmt>) return "compound_assign";
        if constexpr (std::is_same_v<T, DeferStmt>) return "defer";
        if constexpr (std::is_same_v<T, RegionStmt>) return "region";
        if constexpr (std::is_same_v<T, FrameStmt>) return "frame";
        if constexpr (std::is_same_v<T, KeyBlockStmt>) return "key_block";
        if constexpr (std::is_same_v<T, ReturnStmt>) return "return";
        if constexpr (std::is_same_v<T, BreakStmt>) return "break";
        if constexpr (std::is_same_v<T, ContinueStmt>) return "continue";
        if constexpr (std::is_same_v<T, UnsafeBlockStmt>) return "unsafe";
        if constexpr (std::is_same_v<T, CtStmt>) return "comptime";
        return "";
      },
      s);
}

std::string ExprKindTag(const Expr& e) {
  return std::visit(
      [](const auto& node) -> std::string {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralExpr>) return "literal";
        if constexpr (std::is_same_v<T, IdentifierExpr>) return "identifier";
        if constexpr (std::is_same_v<T, FieldAccessExpr>) return "field_access";
        if constexpr (std::is_same_v<T, TupleAccessExpr>) return "tuple_index";
        if constexpr (std::is_same_v<T, IndexAccessExpr>) return "index";
        if constexpr (std::is_same_v<T, IfExpr>) return "if";
        if constexpr (std::is_same_v<T, IfIsExpr>) return "if";
        if constexpr (std::is_same_v<T, IfCaseExpr>) return "if";
        if constexpr (std::is_same_v<T, LoopInfiniteExpr>) return "loop";
        if constexpr (std::is_same_v<T, LoopConditionalExpr>) return "loop";
        if constexpr (std::is_same_v<T, LoopIterExpr>) return "loop";
        if constexpr (std::is_same_v<T, ComptimeExpr>) return "comptime";
        if constexpr (std::is_same_v<T, CtIfExpr>) return "comptime";
        if constexpr (std::is_same_v<T, CtLoopIterExpr>) return "comptime";
        if constexpr (std::is_same_v<T, MoveExpr>) return "move";
        if constexpr (std::is_same_v<T, UnaryExpr>) {
          return node.op == "widen" ? "widen" : "";
        }
        if constexpr (std::is_same_v<T, TransmuteExpr>) return "transmute";
        if constexpr (std::is_same_v<T, UnsafeBlockExpr>) return "unsafe";
        if constexpr (std::is_same_v<T, AllocExpr>) return "region_alloc";
        if constexpr (std::is_same_v<T, MethodCallExpr>) return "method_call";
        if constexpr (std::is_same_v<T, PropagateExpr>) return "union_propagate";
        if constexpr (std::is_same_v<T, ParallelExpr>) return "parallel";
        if constexpr (std::is_same_v<T, SpawnExpr>) return "spawn";
        if constexpr (std::is_same_v<T, DispatchExpr>) return "dispatch";
        if constexpr (std::is_same_v<T, WaitExpr>) return "wait";
        if constexpr (std::is_same_v<T, YieldExpr>) return "yield";
        if constexpr (std::is_same_v<T, YieldFromExpr>) return "yield";
        if constexpr (std::is_same_v<T, SyncExpr>) return "sync";
        if constexpr (std::is_same_v<T, RaceExpr>) return "race";
        if constexpr (std::is_same_v<T, AllExpr>) return "all";
        return "";
      },
      e.node);
}

}  // namespace

std::vector<const Type*> type_nodes(const ASTModule& module) {
  std::vector<const Type*> out;
  for (const auto& item : module.items) {
    collect_type_nodes_from_item(item, out);
  }
  return out;
}

std::vector<const Stmt*> stmt_nodes(const ASTModule& module) {
  std::vector<const Stmt*> out;
  for (const auto& item : module.items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ProcedureDecl> ||
                        std::is_same_v<T, ComptimeProcedureDecl> ||
                        std::is_same_v<T, DeriveTargetDecl>) {
            collect_stmt_nodes_from_block(node.body, out);
          } else if constexpr (std::is_same_v<T, RecordDecl>) {
            for (const auto& member : node.members) {
              if (const auto* method = std::get_if<MethodDecl>(&member)) {
                collect_stmt_nodes_from_block(method->body, out);
              }
            }
          } else if constexpr (std::is_same_v<T, ModalDecl>) {
            for (const auto& state : node.states) {
              for (const auto& member : state.members) {
                if (const auto* method = std::get_if<StateMethodDecl>(&member)) {
                  collect_stmt_nodes_from_block(method->body, out);
                } else if (const auto* transition =
                               std::get_if<TransitionDecl>(&member)) {
                  collect_stmt_nodes_from_block(transition->body, out);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ClassDecl>) {
            for (const auto& class_item : node.items) {
              if (const auto* method = std::get_if<ClassMethodDecl>(&class_item)) {
                collect_stmt_nodes_from_block(method->body_opt, out);
              }
            }
          } else if constexpr (std::is_same_v<T, ExternBlock>) {
            for (const auto& ext : node.items) {
              std::visit(
                  [&](const auto& ext_item) {
                    (void)ext_item;
                  },
                  ext);
            }
          }
        },
        item);
  }
  return out;
}

std::vector<std::string> top_decl_constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto& item : module.items) {
    const std::string kind = ItemKindTag(item);
    if (!kind.empty()) {
      out.insert(kind);
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::string type_ctor(const Type& t) {
  return TypeCtorTag(t);
}

std::vector<std::string> type_constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* type : type_nodes(module)) {
    out.insert(TypeCtorTag(*type));
  }
  return std::vector<std::string>(out.begin(), out.end());
}

namespace {

void InsertReceiverShorthandPerm(const Receiver& receiver, std::set<std::string>& out) {
  const auto* recv = std::get_if<ReceiverShorthand>(&receiver);
  if (recv == nullptr) {
    return;
  }

  switch (recv->perm) {
    case ReceiverPerm::Const:
      out.insert("const");
      break;
    case ReceiverPerm::Unique:
      out.insert("unique");
      break;
    case ReceiverPerm::Shared:
      out.insert("shared");
      break;
  }
}

}  // namespace

std::vector<std::string> recv_perms(const std::vector<RecordMember>& members) {
  std::set<std::string> out;
  for (const auto& member : members) {
    const auto* method = std::get_if<MethodDecl>(&member);
    if (method == nullptr) {
      continue;
    }

    InsertReceiverShorthandPerm(method->receiver, out);
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> class_recv_perms(const std::vector<ClassItem>& items) {
  std::set<std::string> out;
  for (const auto& item : items) {
    const auto* method = std::get_if<ClassMethodDecl>(&item);
    if (method == nullptr) {
      continue;
    }

    InsertReceiverShorthandPerm(method->receiver, out);
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> state_recv_perms(const std::vector<StateBlock>& states) {
  std::set<std::string> out;
  for (const auto& state : states) {
    for (const auto& member : state.members) {
      const auto* method = std::get_if<StateMethodDecl>(&member);
      if (method == nullptr) {
        continue;
      }

      InsertReceiverShorthandPerm(method->receiver, out);
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> perm_constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* type : type_nodes(module)) {
    if (const auto* perm = std::get_if<TypePermType>(&type->node)) {
      switch (perm->perm) {
        case TypePerm::Const:
          out.insert("const");
          break;
        case TypePerm::Unique:
          out.insert("unique");
          break;
        case TypePerm::Shared:
          out.insert("shared");
          break;
      }
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::string expr_kind(const Expr& e) {
  return ExprKindTag(e);
}

std::vector<std::string> expr_stmt_constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* stmt : stmt_nodes(module)) {
    const std::string stmt_kind = StmtKindTag(*stmt);
    if (!stmt_kind.empty()) {
      out.insert(stmt_kind);
    }
    if (const auto* expr_stmt = std::get_if<ExprStmt>(stmt)) {
      if (expr_stmt->value) {
        const std::string expr_kind_tag = ExprKindTag(*expr_stmt->value);
        if (!expr_kind_tag.empty()) {
          out.insert(expr_kind_tag);
        }
      }
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> cap_constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* type : type_nodes(module)) {
    if (const auto* dynamic = std::get_if<TypeDynamic>(&type->node)) {
      out.insert("$" + JoinPath(dynamic->path));
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> constructs(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto& v : top_decl_constructs(module)) out.insert(v);
  for (const auto& v : type_constructs(module)) out.insert(v);
  for (const auto& v : perm_constructs(module)) out.insert(v);
  for (const auto& v : expr_stmt_constructs(module)) out.insert(v);
  for (const auto& v : cap_constructs(module)) out.insert(v);
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> appendix_item_forms(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto& item : module.items) {
    const std::string form = AppendixItemFormTag(item);
    if (!form.empty()) {
      out.insert(form);
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> appendix_type_forms(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* type : type_nodes(module)) {
    const std::string form = AppendixTypeFormTag(*type);
    if (!form.empty()) {
      out.insert(form);
    }
  }
  return std::vector<std::string>(out.begin(), out.end());
}

std::vector<std::string> appendix_ct_family_forms(const ASTModule& module) {
  std::set<std::string> out;
  for (const auto* stmt : stmt_nodes(module)) {
    if (stmt != nullptr && std::holds_alternative<CtStmt>(*stmt)) {
      out.insert("CtStmt");
    }
  }

  std::vector<const Expr*> exprs;
  for (const auto& item : module.items) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ProcedureDecl> ||
                        std::is_same_v<T, ComptimeProcedureDecl> ||
                        std::is_same_v<T, DeriveTargetDecl>) {
            if constexpr (std::is_same_v<T, ComptimeProcedureDecl>) {
              if (node.contract && node.contract->precondition) {
                collect_expr_nodes_from_expr(node.contract->precondition, exprs);
              }
              if (node.contract && node.contract->postcondition) {
                collect_expr_nodes_from_expr(node.contract->postcondition, exprs);
              }
            }
            collect_expr_nodes_from_block(node.body, exprs);
          } else if constexpr (std::is_same_v<T, RecordDecl>) {
            for (const auto& member : node.members) {
              if (const auto* method = std::get_if<MethodDecl>(&member)) {
                collect_expr_nodes_from_block(method->body, exprs);
              }
            }
          } else if constexpr (std::is_same_v<T, ModalDecl>) {
            for (const auto& state : node.states) {
              for (const auto& member : state.members) {
                if (const auto* method = std::get_if<StateMethodDecl>(&member)) {
                  collect_expr_nodes_from_block(method->body, exprs);
                } else if (const auto* transition =
                               std::get_if<TransitionDecl>(&member)) {
                  collect_expr_nodes_from_block(transition->body, exprs);
                }
              }
            }
          } else if constexpr (std::is_same_v<T, ClassDecl>) {
            for (const auto& class_item : node.items) {
              if (const auto* method = std::get_if<ClassMethodDecl>(&class_item)) {
                collect_expr_nodes_from_block(method->body_opt, exprs);
              }
            }
          }
        },
        item);
  }

  for (const auto* expr : exprs) {
    std::visit(
        [&](const auto& node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ComptimeExpr>) {
            out.insert("CtExpr");
          } else if constexpr (std::is_same_v<T, CtIfExpr>) {
            out.insert("CtIf");
          } else if constexpr (std::is_same_v<T, CtLoopIterExpr>) {
            out.insert("CtLoopIter");
          } else if constexpr (std::is_same_v<T, TypeLiteralExpr>) {
            out.insert("Type::<...>");
          } else if constexpr (std::is_same_v<T, QuoteExpr>) {
            if (node.kind == QuoteKind::Type) {
              out.insert("quote_type");
            } else if (node.kind == QuoteKind::Pattern) {
              out.insert("quote_pattern");
            } else {
              out.insert("quote_expr");
            }
            for (const auto& tok : node.tokens) {
              if (tok.lexeme == "$(" || tok.lexeme == "$") {
                out.insert("splice_form");
                break;
              }
            }
          }
        },
        expr->node);
  }

  return std::vector<std::string>(out.begin(), out.end());
}

}  // namespace ultraviolet::ast
