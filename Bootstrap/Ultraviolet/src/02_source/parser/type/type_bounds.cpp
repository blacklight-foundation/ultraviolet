// =============================================================================
// type_bounds.cpp - Type Bounds Parsing for Generic Parameters
// =============================================================================
//
// SPEC REFERENCE: Docs/SPECIFICATION.md, Section 3.3.6.13, Lines 3976-4009
//
// Parses type bounds: <: Bound1, Bound2 used in generic parameter declarations.
// Note: Type bounds parsing is part of generic parameter parsing, which is
// handled in the items/generic_params.cpp module. This file exists for
// organizational purposes.
//
// =============================================================================

#include "02_source/parser/type/type_parse_internal.h"

#include "00_core/assert_spec.h"

namespace ultraviolet::ast {

// =============================================================================
// Type Bounds Parsing
// =============================================================================
// Note: Type bounds (<: Bound1, Bound2) are parsed as part of generic
// parameter declarations in items/generic_params.cpp, not in the type
// parsing module. This file exists for organizational modularity.

}  // namespace ultraviolet::ast
