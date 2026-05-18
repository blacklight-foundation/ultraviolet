// =============================================================================
// literal_pattern.cpp - Literal Pattern Parsing
// =============================================================================
//
// SPEC REFERENCE: SPECIFICATION.md Section 3.3.9, Lines 6087-6090
//
// Literal patterns match constant values (integers, floats, strings, chars,
// bools, null). The parsing logic is implemented in pattern_common.cpp as
// part of ParsePatternAtom.
//
// FORMAL RULE:
// **(Parse-Pattern-Literal)**
// Tok(P).kind in {IntLiteral, FloatLiteral, StringLiteral, CharLiteral,
//                 BoolLiteral, NullLiteral}
// --------------------------------------------------------------------------
// Gamma |- ParsePatternAtom(P) => (Advance(P), LiteralPattern(Tok(P)))
//
// IMPLEMENTATION: See pattern_common.cpp, ParsePatternAtom, branch 1
//
// =============================================================================

// This file intentionally minimal - literal pattern parsing is implemented
// in pattern_common.cpp as part of the unified ParsePatternAtom dispatch.
