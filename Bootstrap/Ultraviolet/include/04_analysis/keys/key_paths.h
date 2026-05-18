#pragma once

// =============================================================================
// Key Path Construction and Analysis
// =============================================================================
//
// SPEC REFERENCE:
//   - Docs/SPECIFICATION.md, Section 17.1.7 "Key Roots and Key Path Formation" (lines 23938-23988)
//   - Docs/SPECIFICATION.md, Section 17.2.4 "Key Boundaries" (lines 24100-24150)
//   - Docs/SPECIFICATION.md, Section 17.3 "Path Overlap" (lines 24200-24250)
//
// KEY PATH SEMANTICS (from spec):
//   - Key paths represent memory locations for synchronization
//   - Paths are built from field access chains: x.field1.field2
//   - Array indexing creates indexed paths: x[i]
//   - # prefix on fields creates key boundaries
//
// PATH CONSTRUCTION (from spec):
//   Root(e) =
//     x                 if e = x
//     Root(e')          if e = e'.f
//     Root(e')          if e = e'[i]
//     Root(e')          if e = e' ~> m(...)
//     bottom_boundary   if e = (*e')
//
// KEY BOUNDARIES:
//   - # marker on field creates key boundary
//   - Pointer dereference (*e) creates boundary
//   - Type-level boundaries from record definitions
//   - Key paths do not extend across boundaries
//
// PATH OVERLAP (from spec):
//   Overlap(P1, P2) iff Prefix(P1, P2) or Prefix(P2, P1)
//
// =============================================================================

#include <optional>
#include <string>
#include <vector>

#include "00_core/span.h"
#include "02_source/ast/ast.h"
#include "04_analysis/keys/key_context.h"

namespace ultraviolet::analysis {

// =============================================================================
// Path Construction Result
// =============================================================================

/// Result of key path construction from an expression
struct KeyPathResult {
  bool success = false;
  KeyPath path;
  std::optional<std::string_view> error_code;
  std::optional<core::Span> error_span;

  /// True if path construction hit a boundary
  bool hit_boundary = false;

  /// The boundary type if hit_boundary is true
  enum class BoundaryKind {
    None,
    PointerDeref,    // *e creates boundary
    FieldMarker,     // # on field
    TypeBoundary,    // Type-level boundary
  };
  BoundaryKind boundary_kind = BoundaryKind::None;
};

// =============================================================================
// Path Analysis Results
// =============================================================================

/// Result of path overlap analysis
struct OverlapResult {
  bool overlaps = false;
  bool p1_prefix_of_p2 = false;
  bool p2_prefix_of_p1 = false;

  /// The common prefix if paths overlap
  std::optional<KeyPath> common_prefix;
};

/// Result of path containment analysis
struct ContainsResult {
  bool contains = false;
  bool exact_match = false;
  bool prefix_match = false;
};

// =============================================================================
// Key Path Construction Functions
// =============================================================================

/// Build a key path from an expression
/// Traverses the expression tree to construct the path
KeyPathResult BuildKeyPath(const ast::ExprPtr& expr);

/// Build a key path from a field access expression
/// Recursively builds from base.field
KeyPathResult BuildKeyPathFromFieldAccess(const ast::FieldAccessExpr& field);

/// Build a key path from an index access expression
/// Creates path with index segment
KeyPathResult BuildKeyPathFromIndexAccess(const ast::IndexAccessExpr& index);

/// Build a key path from a method call receiver
/// Handles method chains like x~>foo()~>bar()
KeyPathResult BuildKeyPathFromMethodCall(const ast::MethodCallExpr& method);

/// Build a key path from a dereference expression
/// Note: Dereference creates a boundary, path ends at *e
KeyPathResult BuildKeyPathFromDeref(const ast::DerefExpr& deref);

// =============================================================================
// Path Parsing Functions
// =============================================================================

/// Parse a key path from a KeyPathExpr (from # block syntax)
KeyPath ParseKeyPathSpec(const ast::KeyPathExpr& spec);

/// Parse a key path from a string representation
/// Format: "root.field1[idx].field2"
std::optional<KeyPath> ParseKeyPathString(std::string_view path_str);

// =============================================================================
// Path Normalization Functions
// =============================================================================

/// Normalize a key path for comparison
/// - Removes redundant segments
/// - Standardizes index representation
/// - Truncates at boundaries
KeyPath NormalizeKeyPath(const KeyPath& path);

/// Truncate a key path at the first boundary
/// Returns the path up to (but not including) the boundary
KeyPath TruncateAtBoundary(const KeyPath& path);

/// Find the first boundary in a key path
/// Returns the index of the boundary segment, or nullopt if none
std::optional<size_t> FindKeyBoundary(const KeyPath& path);

// =============================================================================
// Path Comparison Functions
// =============================================================================

/// Check if two key paths overlap
/// Paths overlap if one is a prefix of the other
bool KeyPathsOverlap(const KeyPath& p1, const KeyPath& p2);

/// Detailed overlap analysis
OverlapResult AnalyzeOverlap(const KeyPath& p1, const KeyPath& p2);

/// Check if path1 contains path2
/// path1 contains path2 if path1 is a prefix of path2
bool KeyPathContains(const KeyPath& container, const KeyPath& contained);

/// Detailed containment analysis
ContainsResult AnalyzeContainment(const KeyPath& container, const KeyPath& contained);

/// Check exact path equality
bool KeyPathEquals(const KeyPath& p1, const KeyPath& p2);

/// Compute the common prefix of two paths
/// Returns empty path if no common prefix
KeyPath CommonPrefix(const KeyPath& p1, const KeyPath& p2);

// =============================================================================
// Path Segment Analysis
// =============================================================================

/// Check if a segment is an index segment
bool IsIndexSegment(const KeyPathSeg& seg);

/// Check if a segment is a field segment
bool IsFieldSegment(const KeyPathSeg& seg);

/// Check if a segment has a boundary marker
bool IsBoundarySegment(const KeyPathSeg& seg);

/// Compare two path segments for equality
bool SegmentsEqual(const KeyPathSeg& s1, const KeyPathSeg& s2);

// =============================================================================
// Path Root Analysis
// =============================================================================

/// Extract the root binding name from an expression
/// Returns nullopt if expression is not rooted at a binding
std::optional<std::string> ExtractPathRoot(const ast::ExprPtr& expr);

/// Check if an expression represents a place expression
/// Place expressions can have key paths derived from them
bool IsPlaceExpression(const ast::ExprPtr& expr);

/// Check if expression root is a shared binding
/// This determines whether key acquisition is needed
bool RootIsShared(const ast::ExprPtr& expr);

// =============================================================================
// Boundary Detection Functions
// =============================================================================

/// Check if an expression contains a pointer dereference
/// Pointer dereference creates a key boundary
bool HasPointerDeref(const ast::ExprPtr& expr);

/// Check if a type has a key boundary marker
/// Records with #field have boundaries
bool TypeHasKeyBoundary(const ast::TypePtr& type_ptr);

/// Get boundary fields for a record type
/// Returns field names that have # markers
std::vector<std::string> GetBoundaryFields(const ast::TypePath& record_path);

// =============================================================================
// Utility Functions
// =============================================================================

/// Convert a key path to a human-readable string
std::string KeyPathToString(const KeyPath& path);

/// Convert a key path to a canonical form for hashing
std::string KeyPathToCanonical(const KeyPath& path);

/// Get the depth (number of segments) of a key path
size_t KeyPathDepth(const KeyPath& path);

/// Check if a key path is empty (root only, no segments)
bool KeyPathIsRoot(const KeyPath& path);

}  // namespace ultraviolet::analysis
