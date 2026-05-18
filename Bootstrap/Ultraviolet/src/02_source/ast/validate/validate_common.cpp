// ===========================================================================
// validate_common.cpp - Common Validation Infrastructure
// ===========================================================================
//
// PURPOSE:
//   Common validation infrastructure, result types, and visitor pattern base.
//   Provides foundational types and utilities used by all category-specific
//   validators.
//
// ===========================================================================
// SPEC REFERENCE: Docs/SPECIFICATION.md
// ===========================================================================
//   Section 3.3.2 - AST Node Catalog
//   Defines the AST structure that validation operates on.
//
//   Section 3.3.11 - Documentation Association
//   Validation phases include documentation attachment.
//
// ===========================================================================

#include "02_source/ast/ast.h"
#include "02_source/ast/ast_utils.h"

#include <functional>
#include <initializer_list>
#include <variant>
#include <vector>

#include "00_core/diagnostic_messages.h"
#include "00_core/diagnostics.h"
#include "00_core/span.h"

namespace ultraviolet::ast::validate {

// ===========================================================================
// VALIDATION RESULT
// ===========================================================================

// Result of a validation operation.
// Contains success/failure status and any emitted diagnostics.
struct ValidationResult {
  bool valid = true;
  core::DiagnosticStream diags;

  // Combine two validation results.
  // Result is valid only if both inputs are valid.
  ValidationResult& operator+=(const ValidationResult& other) {
    valid = valid && other.valid;
    for (const auto& diag : other.diags) {
      core::Emit(diags, diag);
    }
    return *this;
  }

  // Check if validation passed.
  explicit operator bool() const { return valid; }
};

// Combine multiple validation results.
ValidationResult combine(std::initializer_list<ValidationResult> results) {
  ValidationResult combined;
  for (const auto& result : results) {
    combined += result;
  }
  return combined;
}

// ===========================================================================
// VALIDATION CONTEXT
// ===========================================================================

// Context passed through validation traversal.
// Tracks current scope, parent nodes, and validation state.
struct ValidationContext {
  core::DiagnosticStream& diags;

  // Scope tracking
  bool in_method_context = false;
  bool in_modal_state = false;
  bool in_unsafe_block = false;
  bool in_region_block = false;
  bool in_loop = false;
  bool in_parallel = false;

  // Key tracking for yield validation
  // SPEC: Keys cannot be held across yield points
  bool keys_held = false;

  // Parent tracking for context-dependent validation
  const ASTItem* current_item = nullptr;
  const Stmt* current_stmt = nullptr;
  const Expr* current_expr = nullptr;

  explicit ValidationContext(core::DiagnosticStream& d) : diags(d) {}

  // Create a child context with modified scope flags
  ValidationContext child() const {
    ValidationContext ctx(diags);
    ctx.in_method_context = in_method_context;
    ctx.in_modal_state = in_modal_state;
    ctx.in_unsafe_block = in_unsafe_block;
    ctx.in_region_block = in_region_block;
    ctx.in_loop = in_loop;
    ctx.in_parallel = in_parallel;
    ctx.keys_held = keys_held;
    ctx.current_item = current_item;
    ctx.current_stmt = current_stmt;
    ctx.current_expr = current_expr;
    return ctx;
  }

  // Enter an unsafe block context
  ValidationContext enter_unsafe() const {
    ValidationContext ctx = child();
    ctx.in_unsafe_block = true;
    return ctx;
  }

  // Enter a region block context
  ValidationContext enter_region() const {
    ValidationContext ctx = child();
    ctx.in_region_block = true;
    return ctx;
  }

  // Enter a loop context
  ValidationContext enter_loop() const {
    ValidationContext ctx = child();
    ctx.in_loop = true;
    return ctx;
  }

  // Enter a parallel block context
  ValidationContext enter_parallel() const {
    ValidationContext ctx = child();
    ctx.in_parallel = true;
    return ctx;
  }

  // Mark that keys are being held
  ValidationContext with_keys_held() const {
    ValidationContext ctx = child();
    ctx.keys_held = true;
    return ctx;
  }
};

// ===========================================================================
// SPAN VALIDATION UTILITIES
// ===========================================================================

void emit_error(ValidationContext& ctx,
                const core::Span& span,
                const core::DiagCode& code,
                const std::string& message);

// Validates that a span has correct ordering (start <= end).
bool validate_span_ordering(const core::Span& span, ValidationContext& ctx) {
  if (span.start_offset > span.end_offset) {
    emit_error(ctx, span, "E-SRC-0520",
               "span start offset exceeds end offset");
    return false;
  }
  return true;
}

// Validates that child span is contained within parent span.
bool validate_span_containment(const core::Span& parent,
                               const core::Span& child,
                               ValidationContext& ctx) {
  if (child.start_offset < parent.start_offset ||
      child.end_offset > parent.end_offset) {
    emit_error(ctx, child, "E-SRC-0520",
               "child span is not contained within parent span");
    return false;
  }
  return true;
}

// Validates that sibling spans do not overlap.
bool validate_span_no_overlap(const core::Span& a,
                              const core::Span& b,
                              ValidationContext& ctx) {
  // Spans overlap if neither ends before the other starts
  bool overlaps = !(a.end_offset <= b.start_offset ||
                    b.end_offset <= a.start_offset);
  if (overlaps) {
    emit_error(ctx, a, "E-SRC-0520", "sibling spans overlap");
    return false;
  }
  return true;
}

// ===========================================================================
// DIAGNOSTIC HELPERS
// ===========================================================================

core::Diagnostic make_internal_diagnostic(core::Severity severity,
                                          const std::optional<core::Span>& span,
                                          const std::string& message) {
  core::Diagnostic diag;
  diag.severity = severity;
  diag.span = span;
  diag.message = message;
  return diag;
}

// Emit a validation error.
void emit_error(ValidationContext& ctx,
                const core::Span& span,
                const core::DiagCode& code,
                const std::string& message) {
  if (auto diag = core::MakeDiagnosticById(code, span)) {
    diag->severity = core::Severity::Error;
    diag->message = message;
    core::Emit(ctx.diags, *diag);
    return;
  }
  core::Emit(ctx.diags, make_internal_diagnostic(
                            core::Severity::Error, span,
                            "Internal error: unresolved validation diagnostic id '" +
                                code + "'"));
}

// Emit a validation warning.
void emit_warning(ValidationContext& ctx,
                  const core::Span& span,
                  const core::DiagCode& code,
                  const std::string& message) {
  if (auto diag = core::MakeDiagnosticById(code, span)) {
    diag->severity = core::Severity::Warning;
    diag->message = message;
    core::Emit(ctx.diags, *diag);
    return;
  }
  core::Emit(ctx.diags, make_internal_diagnostic(
                            core::Severity::Warning, span,
                            "Internal warning: unresolved validation diagnostic id '" +
                                code + "'"));
}

// Emit a validation info/note.
void emit_info(ValidationContext& ctx,
               const core::Span& span,
               const std::string& message) {
  core::Diagnostic diag;
  diag.severity = core::Severity::Warning;
  diag.message = message;
  diag.span = span;
  core::Emit(ctx.diags, diag);
}

// ===========================================================================
// COMPOSABLE VALIDATION
// ===========================================================================

// Type alias for validation functions.
using ValidatorFn =
    std::function<ValidationResult(const ASTModule&, ValidationContext&)>;

// Compose multiple validators into a single validator.
// Runs each validator in sequence, combining results.
ValidatorFn compose(std::initializer_list<ValidatorFn> validators) {
  std::vector<ValidatorFn> fns(validators);
  return [fns](const ASTModule& module, ValidationContext& ctx) {
    ValidationResult result;
    for (const auto& fn : fns) {
      result += fn(module, ctx);
    }
    return result;
  };
}

// Create a validator that runs only if a condition is met.
ValidatorFn when(bool condition, ValidatorFn validator) {
  return [condition, validator](const ASTModule& module,
                                ValidationContext& ctx) {
    if (condition) {
      return validator(module, ctx);
    }
    return ValidationResult{};
  };
}

// ===========================================================================
// BASIC VALIDATION FUNCTIONS
// ===========================================================================

// Validate that an expression is in a valid context.
// Some expressions are only allowed in specific contexts.
ValidationResult validate_expr_context(const Expr& expr,
                                       ValidationContext& ctx) {
  ValidationResult result;

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        // Yield expressions require we're not holding keys
        if constexpr (std::is_same_v<T, YieldExpr> ||
                      std::is_same_v<T, YieldFromExpr>) {
          if (ctx.keys_held) {
            // Check for release modifier
            bool has_release = false;
            if constexpr (std::is_same_v<T, YieldExpr>) {
              has_release = node.release;
            }
            if constexpr (std::is_same_v<T, YieldFromExpr>) {
              has_release = node.release;
            }

            if (!has_release) {
              emit_error(ctx, expr.span, "E-CON-0213",
                         "yield while holding keys requires 'release' modifier");
              result.valid = false;
            }
          }
        }

        // Transmute requires unsafe context
        if constexpr (std::is_same_v<T, TransmuteExpr>) {
          if (!ctx.in_unsafe_block) {
            emit_error(ctx, expr.span, "E-MEM-3030",
                       "transmute requires unsafe block");
            result.valid = false;
          }
        }

        // Alloc (^) requires region context
        if constexpr (std::is_same_v<T, AllocExpr>) {
          if (!ctx.in_region_block && !node.region_opt.has_value()) {
            emit_error(ctx, expr.span, "E-MEM-3021",
                       "allocation (^) requires region context");
            result.valid = false;
          }
        }

        // Break/continue expressions need loop context
        // (These are statements, not expressions, but included for completeness)
      },
      expr.node);

  return result;
}

// Validate that a statement is in a valid context.
ValidationResult validate_stmt_context(const Stmt& stmt,
                                       ValidationContext& ctx) {
  ValidationResult result;

  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;

        // Break requires loop context
        if constexpr (std::is_same_v<T, BreakStmt>) {
          if (!ctx.in_loop) {
            emit_error(ctx, node.span, "E-SEM-3162",
                       "break statement outside of loop");
            result.valid = false;
          }
        }

        // Continue requires loop context
        if constexpr (std::is_same_v<T, ContinueStmt>) {
          if (!ctx.in_loop) {
            emit_error(ctx, node.span, "E-SEM-3163",
                       "continue statement outside of loop");
            result.valid = false;
          }
        }

        // Frame requires region context
        if constexpr (std::is_same_v<T, FrameStmt>) {
          if (!ctx.in_region_block) {
            emit_error(ctx, node.span, "E-MEM-1207",
                       "frame statement outside of region block");
            result.valid = false;
          }
        }
      },
      stmt);

  return result;
}

// Validate that a pattern is irrefutable (for let/var bindings).
ValidationResult validate_irrefutable_pattern(const Pattern& pattern,
                                              ValidationContext& ctx) {
  ValidationResult result;

  if (is_refutable(pattern.node)) {
    emit_error(ctx, pattern.span, "E-SEM-2711",
               "refutable pattern in binding position");
    result.valid = false;
  }

  return result;
}

}  // namespace ultraviolet::ast::validate

