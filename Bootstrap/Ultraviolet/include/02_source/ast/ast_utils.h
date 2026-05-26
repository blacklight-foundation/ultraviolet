#pragma once

// ===========================================================================
// ast_utils.h - AST Utility Function Declarations
// ===========================================================================
//
// PURPOSE:
//   Declares utility functions for AST node inspection, span extraction,
//   and category checks. These are implemented in ast_nodes.cpp.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md Section 5.2 and Appendix C
//
// ===========================================================================

#include "00_core/span.h"
#include "02_source/ast/ast.h"

namespace ultraviolet::ast {

// ===========================================================================
// SPAN EXTRACTION
// ===========================================================================
// SpanOfNode : ASTNode -> Span
// Extracts the source span from any AST node type.

core::Span span_of(const Expr& e);
core::Span span_of(const Type& t);
core::Span span_of(const Pattern& p);
core::Span span_of(const Block& b);
core::Span span_of(const Stmt& s);
core::Span span_of(const ASTItem& item);

// ===========================================================================
// DOCUMENTATION EXTRACTION
// ===========================================================================
// DocOf : ASTNode -> (DocList | bottom)
// Returns pointer to documentation list, or nullptr if not present.

const DocList* doc_of(const Expr& e);
const DocList* doc_of(const Type& t);
const DocList* doc_of(const Pattern& p);
const DocList* doc_of(const Stmt& s);
const DocList* doc_of(const ASTItem& item);

// ===========================================================================
// NODE KIND STRINGS
// ===========================================================================
// Returns a human-readable string identifying the node type.

const char* node_kind(const Expr& e);
const char* node_kind(const Type& t);
const char* node_kind(const Pattern& p);
const char* node_kind(const Stmt& s);
const char* node_kind(const ASTItem& item);

// ===========================================================================
// EXPRESSION CATEGORY CHECKS
// ===========================================================================

bool is_literal(const ExprNode& n);
bool is_binary_op(const ExprNode& n);
bool is_control_flow(const ExprNode& n);
bool is_memory_op(const ExprNode& n);
bool is_async(const ExprNode& n);
bool is_concurrency(const ExprNode& n);

// ===========================================================================
// PATTERN CATEGORY CHECKS
// ===========================================================================
// Irrefutable patterns always match; refutable patterns may fail.

bool is_irrefutable(const PatternNode& p);
bool is_refutable(const PatternNode& p);

// ===========================================================================
// STATEMENT CATEGORY CHECKS
// ===========================================================================

bool is_binding(const Stmt& s);
bool is_control(const Stmt& s);
bool is_assignment(const Stmt& s);
bool is_resource_stmt(const Stmt& s);

// ===========================================================================
// ITEM CATEGORY CHECKS
// ===========================================================================

bool is_declaration(const ASTItem& item);
bool is_type_definition(const ASTItem& item);
bool is_value_definition(const ASTItem& item);
bool is_import_or_using(const ASTItem& item);

// ===========================================================================
// CONSTRUCT TAXONOMY HELPERS
// ===========================================================================

std::vector<const Type*> type_nodes(const ASTModule& module);
std::vector<const Stmt*> stmt_nodes(const ASTModule& module);
std::vector<std::string> top_decl_constructs(const ASTModule& module);
std::string type_ctor(const Type& t);
std::vector<std::string> type_constructs(const ASTModule& module);
std::vector<std::string> recv_perms(const std::vector<RecordMember>& members);
std::vector<std::string> class_recv_perms(const std::vector<ClassItem>& items);
std::vector<std::string> state_recv_perms(const std::vector<StateBlock>& states);
std::vector<std::string> perm_constructs(const ASTModule& module);
std::string expr_kind(const Expr& e);
std::vector<std::string> expr_stmt_constructs(const ASTModule& module);
std::vector<std::string> cap_constructs(const ASTModule& module);
std::vector<std::string> constructs(const ASTModule& module);
std::vector<std::string> appendix_item_forms(const ASTModule& module);
std::vector<std::string> appendix_type_forms(const ASTModule& module);
std::vector<std::string> appendix_ct_family_forms(const ASTModule& module);

}  // namespace ultraviolet::ast
