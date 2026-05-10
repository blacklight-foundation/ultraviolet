// =============================================================================
// Key Path Implementation
// =============================================================================
//
// SPEC REFERENCE:
//   - CursiveSpecification.md, Section 17.1.7 "Key Roots and Key Path Formation" (lines 23938-23988)
//   - CursiveSpecification.md, Section 17.2.4 "Key Boundaries" (lines 24100-24150)
//   - CursiveSpecification.md, Section 17.3 "Path Overlap" (lines 24200-24250)
//
// PATH ROOT EXTRACTION (from spec):
//   Root(e) =
//     x                 if e = x (identifier)
//     Root(e')          if e = e'.f (field access)
//     Root(e')          if e = e'[i] (index access)
//     Root(e')          if e = e' ~> m(...) (method call)
//     bottom_boundary   if e = (*e') (pointer dereference)
//
// KEY PATH FORMATION (from spec):
//   For place expression e with shared permission:
//   - Lexical root case: KeyPath(e) = x.p_2...p_n truncated at boundary
//   - Boundary root case: KeyPath(e) = id(*e').p_2...p_n truncated at boundary
//
// BOUNDARY RULES (from spec):
//   - Pointer dereference establishes new key boundary
//   - # marker on field creates key boundary
//   - Key paths do not extend across boundaries
//
// =============================================================================

#include "04_analysis/keys/key_paths.h"

#include <algorithm>
#include <sstream>

#include "00_core/assert_spec.h"
#include "04_analysis/contracts/verification.h"

namespace cursive::analysis {

namespace {

static inline void SpecDefsKeyPaths() {
  SPEC_DEF("KeyPath", "C0X.17.1.7");
  SPEC_DEF("KeyRoot", "C0X.17.1.7");
  SPEC_DEF("KeyBoundary", "C0X.17.2.4");
  SPEC_DEF("PathOverlap", "C0X.17.3");
}

std::string JoinAstPath(const ast::Path& path) {
  std::ostringstream oss;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      oss << "::";
    }
    oss << path[i];
  }
  return oss.str();
}

std::string CanonicalExprIdentity(const ast::ExprPtr& expr) {
  if (!expr) {
    return "?";
  }

  const auto constant = EvaluateConstant(expr);
  if (constant.known) {
    if (constant.is_bool) {
      return constant.bool_value ? "true" : "false";
    }
    return std::to_string(constant.value);
  }

  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }

  if (const auto* literal = std::get_if<ast::LiteralExpr>(&expr->node)) {
    return literal->literal.lexeme;
  }

  if (const auto* qname = std::get_if<ast::QualifiedNameExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << JoinAstPath(qname->path) << "::" << qname->name;
    return oss.str();
  }

  if (const auto* path = std::get_if<ast::PathExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << JoinAstPath(path->path) << "::" << path->name;
    return oss.str();
  }

  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(field->base) << "." << field->name;
    return oss.str();
  }

  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(tuple->base) << "."
        << ast::FormatTupleIndex(tuple->index);
    return oss.str();
  }

  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(index->base) << "["
        << CanonicalExprIdentity(index->index) << "]";
    return oss.str();
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(" << unary->op << CanonicalExprIdentity(unary->value) << ")";
    return oss.str();
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(" << CanonicalExprIdentity(binary->lhs) << binary->op
        << CanonicalExprIdentity(binary->rhs) << ")";
    return oss.str();
  }

  if (const auto* cast = std::get_if<ast::CastExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(" << CanonicalExprIdentity(cast->value) << " as _)";
    return oss.str();
  }

  if (const auto* move = std::get_if<ast::MoveExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(move " << CanonicalExprIdentity(move->place) << ")";
    return oss.str();
  }

  if (const auto* address = std::get_if<ast::AddressOfExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(&" << CanonicalExprIdentity(address->place) << ")";
    return oss.str();
  }

  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << "(*" << CanonicalExprIdentity(deref->value) << ")";
    return oss.str();
  }

  if (const auto* call = std::get_if<ast::CallExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(call->callee) << "(";
    for (size_t i = 0; i < call->args.size(); ++i) {
      if (i > 0) {
        oss << ",";
      }
      if (call->args[i].moved) {
        oss << "move ";
      }
      oss << CanonicalExprIdentity(call->args[i].value);
    }
    oss << ")";
    return oss.str();
  }

  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(method->receiver) << "~>" << method->name << "(";
    for (size_t i = 0; i < method->args.size(); ++i) {
      if (i > 0) {
        oss << ",";
      }
      if (method->args[i].moved) {
        oss << "move ";
      }
      oss << CanonicalExprIdentity(method->args[i].value);
    }
    oss << ")";
    return oss.str();
  }

  if (const auto* propagate = std::get_if<ast::PropagateExpr>(&expr->node)) {
    std::ostringstream oss;
    oss << CanonicalExprIdentity(propagate->value) << "?";
    return oss.str();
  }

  if (const auto* attributed = std::get_if<ast::AttributedExpr>(&expr->node)) {
    return CanonicalExprIdentity(attributed->expr);
  }

  if (const auto* range = std::get_if<ast::RangeExpr>(&expr->node)) {
    const std::string lhs = range->lhs ? CanonicalExprIdentity(range->lhs) : "";
    const std::string rhs = range->rhs ? CanonicalExprIdentity(range->rhs) : "";
    switch (range->kind) {
      case ast::RangeKind::To:
        return ".." + rhs;
      case ast::RangeKind::ToInclusive:
        return "..=" + rhs;
      case ast::RangeKind::Full:
        return "..";
      case ast::RangeKind::From:
        return lhs + "..";
      case ast::RangeKind::Exclusive:
        return lhs + ".." + rhs;
      case ast::RangeKind::Inclusive:
        return lhs + "..=" + rhs;
    }
  }

  std::ostringstream fallback;
  fallback << "$expr" << expr->node.index() << "@"
           << expr->span.start_offset << ":" << expr->span.end_offset;
  return fallback.str();
}

KeyPath LowerKeyPathWithIndexIdentity(const ast::KeyPathExpr& spec) {
  KeyPath path;
  path.root = spec.root;

  for (const auto& seg : spec.segs) {
    KeyPathSeg lowered;
    if (const auto* field = std::get_if<ast::KeySegField>(&seg)) {
      lowered.boundary = false;
      lowered.name = field->name;
      lowered.is_index = false;
      path.segs.push_back(std::move(lowered));
      if (field->marked) {
        break;
      }
    } else if (const auto* index = std::get_if<ast::KeySegIndex>(&seg)) {
      lowered.boundary = false;
      lowered.name = CanonicalExprIdentity(index->expr);
      lowered.is_index = true;
      path.segs.push_back(std::move(lowered));
      if (index->marked) {
        break;
      }
    }
  }

  return path;
}

/// Helper to build path segments from field access chain
void BuildPathSegments(const ast::ExprPtr& expr,
                       std::vector<KeyPathSeg>& segs,
                       bool& hit_boundary,
                       KeyPathResult::BoundaryKind& boundary_kind) {
  if (!expr) return;

  // Field access: base.field
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    // First process base
    BuildPathSegments(field->base, segs, hit_boundary, boundary_kind);
    if (hit_boundary) return;  // Stop at boundary

    // Add this field segment
    KeyPathSeg seg;
    seg.name = field->name;
    seg.is_index = false;
    seg.boundary = false;  // Field access itself doesn't create boundary
    segs.push_back(std::move(seg));
    return;
  }

  // Index access: base[index]
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    // First process base
    BuildPathSegments(index->base, segs, hit_boundary, boundary_kind);
    if (hit_boundary) return;

    // Add index segment
    KeyPathSeg seg;
    seg.is_index = true;
    seg.boundary = false;
    seg.name = CanonicalExprIdentity(index->index);
    segs.push_back(std::move(seg));
    return;
  }

  // Tuple access: base.N
  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    BuildPathSegments(tuple->base, segs, hit_boundary, boundary_kind);
    if (hit_boundary) {
      return;
    }

    KeyPathSeg seg;
    seg.name = ast::FormatTupleIndex(tuple->index);
    seg.is_index = false;
    seg.boundary = false;
    segs.push_back(std::move(seg));
    return;
  }

  // Dereference: *expr creates boundary
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    hit_boundary = true;
    boundary_kind = KeyPathResult::BoundaryKind::PointerDeref;
    return;
  }

  // Method call: process receiver
  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    BuildPathSegments(method->receiver, segs, hit_boundary, boundary_kind);
    return;
  }

  // Identifier is the root, handled separately
  // Other expressions don't contribute to path segments
}

}  // namespace

// =============================================================================
// Key Path Construction Functions
// =============================================================================

KeyPathResult BuildKeyPath(const ast::ExprPtr& expr) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-BuildPath");

  KeyPathResult result;
  result.success = false;

  if (!expr) {
    result.error_code = "E-CON-0034";  // Invalid key path expression
    return result;
  }

  // Extract root
  auto root_opt = ExtractPathRoot(expr);
  if (!root_opt.has_value()) {
    result.error_code = "E-CON-0031";  // Path not rooted at binding
    result.error_span = expr->span;
    return result;
  }

  result.path.root = *root_opt;

  // Build segments
  std::vector<KeyPathSeg> segs;
  bool hit_boundary = false;
  KeyPathResult::BoundaryKind boundary_kind = KeyPathResult::BoundaryKind::None;

  BuildPathSegments(expr, segs, hit_boundary, boundary_kind);

  result.path.segs = std::move(segs);
  result.hit_boundary = hit_boundary;
  result.boundary_kind = boundary_kind;
  result.success = true;

  return result;
}

KeyPathResult BuildKeyPathFromFieldAccess(const ast::FieldAccessExpr& field) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-BuildPath-Field");

  KeyPathResult result;

  // Build from base
  result = BuildKeyPath(field.base);
  if (!result.success) {
    return result;
  }

  // Add field segment
  KeyPathSeg seg;
  seg.name = field.name;
  seg.is_index = false;
  seg.boundary = false;
  result.path.segs.push_back(std::move(seg));

  return result;
}

KeyPathResult BuildKeyPathFromIndexAccess(const ast::IndexAccessExpr& index) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-BuildPath-Index");

  KeyPathResult result;

  // Build from base
  result = BuildKeyPath(index.base);
  if (!result.success) {
    return result;
  }

  // Add index segment
  KeyPathSeg seg;
  seg.is_index = true;
  seg.boundary = false;
  seg.name = CanonicalExprIdentity(index.index);
  result.path.segs.push_back(std::move(seg));

  return result;
}

KeyPathResult BuildKeyPathFromMethodCall(const ast::MethodCallExpr& method) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-BuildPath-Method");

  // For method calls, the key path is derived from the receiver
  return BuildKeyPath(method.receiver);
}

KeyPathResult BuildKeyPathFromDeref(const ast::DerefExpr& deref) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-BuildPath-Deref");

  KeyPathResult result;

  // Dereference creates a boundary - we can build path from operand
  // but must mark that we hit a boundary
  result = BuildKeyPath(deref.value);
  result.hit_boundary = true;
  result.boundary_kind = KeyPathResult::BoundaryKind::PointerDeref;

  return result;
}

// =============================================================================
// Path Parsing Functions
// =============================================================================

KeyPath ParseKeyPathSpec(const ast::KeyPathExpr& spec) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-ParseKeyPath");

  return LowerKeyPathWithIndexIdentity(spec);
}

std::optional<KeyPath> ParseKeyPathString(std::string_view path_str) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-ParseKeyPathString");

  KeyPath path;

  if (path_str.empty()) {
    return std::nullopt;
  }

  // Parse root (everything before first '.' or '[')
  size_t pos = 0;
  size_t dot_pos = path_str.find('.');
  size_t bracket_pos = path_str.find('[');

  size_t first_sep = std::min(dot_pos, bracket_pos);
  if (first_sep == std::string_view::npos) {
    // Just a root, no segments
    path.root = std::string(path_str);
    return path;
  }

  path.root = std::string(path_str.substr(0, first_sep));
  pos = first_sep;

  // Parse segments
  while (pos < path_str.size()) {
    KeyPathSeg seg;

    if (path_str[pos] == '.') {
      // Field segment
      pos++;  // Skip '.'
      bool marked = false;

      // Check for boundary marker
      if (pos < path_str.size() && path_str[pos] == '#') {
        marked = true;
        pos++;
      }

      // Find end of field name
      size_t end = pos;
      while (end < path_str.size() && path_str[end] != '.' && path_str[end] != '[') {
        end++;
      }

      seg.name = std::string(path_str.substr(pos, end - pos));
      seg.is_index = false;
      path.segs.push_back(std::move(seg));
      if (marked) {
        break;
      }
      pos = end;

    } else if (path_str[pos] == '[') {
      // Index segment
      pos++;  // Skip '['
      bool marked = false;

      // Check for boundary marker
      if (pos < path_str.size() && path_str[pos] == '#') {
        marked = true;
        pos++;
      }

      // Find closing bracket
      size_t end = path_str.find(']', pos);
      if (end == std::string_view::npos) {
        return std::nullopt;  // Malformed
      }

      seg.name = std::string(path_str.substr(pos, end - pos));
      seg.is_index = true;
      path.segs.push_back(std::move(seg));
      if (marked) {
        break;
      }
      pos = end + 1;  // Skip ']'

    } else {
      return std::nullopt;  // Unexpected character
    }
  }

  return path;
}

// =============================================================================
// Path Normalization Functions
// =============================================================================

KeyPath NormalizeKeyPath(const KeyPath& path) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-NormalizePath");

  KeyPath normalized;
  normalized.root = path.root;

  for (const auto& seg : path.segs) {
    // Stop at boundary
    if (seg.boundary) {
      KeyPathSeg boundary_seg = seg;
      normalized.segs.push_back(std::move(boundary_seg));
      break;
    }
    normalized.segs.push_back(seg);
  }

  return normalized;
}

KeyPath TruncateAtBoundary(const KeyPath& path) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-TruncateAtBoundary");

  KeyPath truncated;
  truncated.root = path.root;

  for (const auto& seg : path.segs) {
    if (seg.boundary) {
      // Stop before the boundary segment
      break;
    }
    truncated.segs.push_back(seg);
  }

  return truncated;
}

std::optional<size_t> FindKeyBoundary(const KeyPath& path) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-FindBoundary");

  for (size_t i = 0; i < path.segs.size(); ++i) {
    if (path.segs[i].boundary) {
      return i;
    }
  }
  return std::nullopt;
}

// =============================================================================
// Path Comparison Functions
// =============================================================================

bool KeyPathsOverlap(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-PathsOverlap");

  // Delegate to existing function
  return PathsOverlap(p1, p2);
}

OverlapResult AnalyzeOverlap(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-AnalyzeOverlap");

  OverlapResult result;

  result.p1_prefix_of_p2 = IsPrefix(p1, p2);
  result.p2_prefix_of_p1 = IsPrefix(p2, p1);
  result.overlaps = result.p1_prefix_of_p2 || result.p2_prefix_of_p1;

  if (result.overlaps) {
    result.common_prefix = CommonPrefix(p1, p2);
  }

  return result;
}

bool KeyPathContains(const KeyPath& container, const KeyPath& contained) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-PathContains");

  // container contains contained if container is a prefix of contained
  return IsPrefix(container, contained);
}

ContainsResult AnalyzeContainment(const KeyPath& container, const KeyPath& contained) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-AnalyzeContainment");

  ContainsResult result;

  if (KeyPathEquals(container, contained)) {
    result.contains = true;
    result.exact_match = true;
    result.prefix_match = true;
  } else if (IsPrefix(container, contained)) {
    result.contains = true;
    result.exact_match = false;
    result.prefix_match = true;
  }

  return result;
}

bool KeyPathEquals(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-PathEquals");

  if (p1.root != p2.root) {
    return false;
  }

  if (p1.segs.size() != p2.segs.size()) {
    return false;
  }

  for (size_t i = 0; i < p1.segs.size(); ++i) {
    if (!SegmentsEqual(p1.segs[i], p2.segs[i])) {
      return false;
    }
  }

  return true;
}

KeyPath CommonPrefix(const KeyPath& p1, const KeyPath& p2) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-CommonPrefix");

  KeyPath prefix;

  // Must have same root
  if (p1.root != p2.root) {
    return prefix;  // Empty prefix
  }

  prefix.root = p1.root;

  // Find common segments
  size_t min_len = std::min(p1.segs.size(), p2.segs.size());
  for (size_t i = 0; i < min_len; ++i) {
    if (SegmentsEqual(p1.segs[i], p2.segs[i])) {
      prefix.segs.push_back(p1.segs[i]);
    } else {
      break;
    }
  }

  return prefix;
}

// =============================================================================
// Path Segment Analysis
// =============================================================================

bool IsIndexSegment(const KeyPathSeg& seg) {
  return seg.is_index;
}

bool IsFieldSegment(const KeyPathSeg& seg) {
  return !seg.is_index;
}

bool IsBoundarySegment(const KeyPathSeg& seg) {
  return seg.boundary;
}

bool SegmentsEqual(const KeyPathSeg& s1, const KeyPathSeg& s2) {
  return s1.name == s2.name &&
         s1.is_index == s2.is_index &&
         s1.boundary == s2.boundary;
}

// =============================================================================
// Path Root Analysis
// =============================================================================

std::optional<std::string> ExtractPathRoot(const ast::ExprPtr& expr) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-ExtractRoot");

  if (!expr) return std::nullopt;

  // Direct identifier
  if (const auto* ident = std::get_if<ast::IdentifierExpr>(&expr->node)) {
    return ident->name;
  }

  // Field access - get root from base
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return ExtractPathRoot(field->base);
  }

  // Index access - get root from base
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return ExtractPathRoot(index->base);
  }

  // Method call - get root from receiver
  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    return ExtractPathRoot(method->receiver);
  }

  if (const auto* tuple = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return ExtractPathRoot(tuple->base);
  }

  // Dereference creates boundary - root is special
  if (const auto* deref = std::get_if<ast::DerefExpr>(&expr->node)) {
    // Return a synthetic boundary root
    // In a full implementation, this would be a runtime identity
    return "$deref";
  }

  return std::nullopt;
}

bool IsPlaceExpression(const ast::ExprPtr& expr) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-IsPlace");

  if (!expr) return false;

  // Identifiers are places
  if (std::holds_alternative<ast::IdentifierExpr>(expr->node)) {
    return true;
  }

  // Field access on place is place
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return IsPlaceExpression(field->base);
  }

  // Index access on place is place
  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return IsPlaceExpression(index->base);
  }

  // Dereference is place
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return true;
  }

  // Tuple access on place is place
  if (const auto* tuple_access = std::get_if<ast::TupleAccessExpr>(&expr->node)) {
    return IsPlaceExpression(tuple_access->base);
  }

  return false;
}

bool RootIsShared(const ast::ExprPtr& expr) {
  // This would require type information to determine
  // For now, return false as a conservative default
  // A full implementation would look up the binding's permission
  return false;
}

// =============================================================================
// Boundary Detection Functions
// =============================================================================

bool HasPointerDeref(const ast::ExprPtr& expr) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-HasDeref");

  if (!expr) return false;

  // Check if this expression is a dereference
  if (std::holds_alternative<ast::DerefExpr>(expr->node)) {
    return true;
  }

  // Recursively check sub-expressions
  if (const auto* field = std::get_if<ast::FieldAccessExpr>(&expr->node)) {
    return HasPointerDeref(field->base);
  }

  if (const auto* index = std::get_if<ast::IndexAccessExpr>(&expr->node)) {
    return HasPointerDeref(index->base) || HasPointerDeref(index->index);
  }

  if (const auto* method = std::get_if<ast::MethodCallExpr>(&expr->node)) {
    if (HasPointerDeref(method->receiver)) return true;
    for (const auto& arg : method->args) {
      if (HasPointerDeref(arg.value)) return true;
    }
  }

  if (const auto* binary = std::get_if<ast::BinaryExpr>(&expr->node)) {
    return HasPointerDeref(binary->lhs) || HasPointerDeref(binary->rhs);
  }

  if (const auto* unary = std::get_if<ast::UnaryExpr>(&expr->node)) {
    return HasPointerDeref(unary->value);
  }

  return false;
}

bool TypeHasKeyBoundary(const ast::TypePtr& type_ptr) {
  // This would require looking up the type definition
  // to check for # markers on fields
  // For now, return false as a conservative default
  return false;
}

std::vector<std::string> GetBoundaryFields(const ast::TypePath& record_path) {
  // This would require looking up the record definition
  // and finding fields with # markers
  // For now, return empty
  return {};
}

// =============================================================================
// Utility Functions
// =============================================================================

std::string KeyPathToString(const KeyPath& path) {
  // Use the existing ToString method
  return path.ToString();
}

std::string KeyPathToCanonical(const KeyPath& path) {
  SpecDefsKeyPaths();
  SPEC_RULE("K-PathCanonical");

  // Create a canonical string representation for hashing/comparison
  std::ostringstream oss;
  oss << path.root;

  for (const auto& seg : path.segs) {
    if (seg.boundary) {
      oss << "#";
    }
    if (seg.is_index) {
      oss << "[" << seg.name << "]";
    } else {
      oss << "." << seg.name;
    }
  }

  return oss.str();
}

size_t KeyPathDepth(const KeyPath& path) {
  return path.segs.size();
}

bool KeyPathIsRoot(const KeyPath& path) {
  return path.segs.empty();
}

}  // namespace cursive::analysis
