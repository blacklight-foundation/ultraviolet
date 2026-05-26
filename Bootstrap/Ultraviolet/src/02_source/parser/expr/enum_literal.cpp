// =============================================================================
// MIGRATION MAPPING: enum_literal.cpp
// =============================================================================
// This file should contain parsing logic for enum literal expressions.
// Note: Enum literals are NOT directly parsed as primary expressions in
// Ultraviolet. Instead, they are created during name resolution from
// QualifiedName and QualifiedApply AST nodes.
//
// SPEC REFERENCE: Docs/SPECIFICATION.md
// -----------------------------------------------------------------------------
// **Resolution Rules (NOT Parsing Rules)** (Lines 7379-7426)
//
// EnumLiteral is created by ResolveQualifiedForm, not by parsing:
//
// **(ResolveQualifiedForm-EnumUnit)** Lines 7379-7381
//   Γ ⊢ ResolveEnumPath(path, name) ⇓ p
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ResolveQualifiedForm(QualifiedName(path, name)) ⇓ EnumLiteral(FullPath(p, name), ⊥)
//
// **(ResolveQualifiedForm-EnumTuple)** Lines 7404-7406
//   Γ ⊢ ResolveArgs(args) ⇓ args'    Γ ⊢ ResolveEnumTuple(path, name) ⇓ p
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ResolveQualifiedForm(QualifiedApply(path, name, Paren(args))) ⇓
//       EnumLiteral(FullPath(p, name), Paren(ArgsExprs(args')))
//
// **(ResolveQualifiedForm-EnumRecord)** Lines 7419-7423
//   Γ ⊢ ResolveFieldInits(fields) ⇓ fields'
//   Γ ⊢ ResolveRecordPath(path, name) ↑    Γ ⊢ ResolveEnumRecord(path, name) ⇓ p
//   ────────────────────────────────────────────────────────────────────────────
//   Γ ⊢ ResolveQualifiedForm(QualifiedApply(path, name, Brace(fields))) ⇓
//       EnumLiteral(FullPath(p, name), Brace(fields'))
//
// **Type Checking** (Lines 10204-10214)
//
// **(T-EnumLiteral-Unit)** Lines 10202-10204
//   EnumDecl(EnumPath(path)) = E    v = VariantName(path)    VariantPayload(E, v) = UnitPayload
//   ────────────────────────────────────────────────────────────────────────────
//   Γ; R; L ⊢ EnumLiteral(path, ⊥) : TypePath(EnumPath(path))
//
// **(T-EnumLiteral-Tuple)** Lines 10206-10209
//   EnumDecl(EnumPath(path)) = E    v = VariantName(path)
//   VariantPayload(E, v) = TuplePayload(T_1, ..., T_n)
//   ∀ i ∈ 1..n. Γ; R; L ⊢ e_i ⇐ T_i ⊣ ∅
//   ────────────────────────────────────────────────────────────────────────────
//   Γ; R; L ⊢ EnumLiteral(path, Paren([e_1, …, e_n])) : TypePath(EnumPath(path))
//
// **(T-EnumLiteral-Record)** Lines 10210-10214
//   EnumDecl(EnumPath(path)) = E    v = VariantName(path)
//   VariantPayload(E, v) = RecordPayload(io)    Distinct(FieldInitNames(fields))
//   FieldInitSet(fields) = VariantFieldNameSet(io)
//   ∀ ⟨f, e⟩ ∈ fields, Γ; R; L ⊢ e ⇐ EnumFieldType(E, v, f) ⊣ ∅
//   ────────────────────────────────────────────────────────────────────────────
//   Γ; R; L ⊢ EnumLiteral(path, Brace(fields)) : TypePath(EnumPath(path))
//
// SEMANTICS:
// - EnumLiteral nodes are NOT created during parsing
// - QualifiedName and QualifiedApply are parsed instead
// - During name resolution (phase 3), these are resolved to EnumLiteral
// - EnumLiteral has three forms: unit, tuple, and record payload
//   - Unit: EnumPath::Variant (no payload)
//   - Tuple: EnumPath::Variant(e1, e2, ...) (positional args)
//   - Record: EnumPath::Variant{ f1: e1, f2: e2, ... } (named fields)
//
// SOURCE FILE: N/A for parsing (enum literals created in resolution phase)
//
// CONTENT TO MIGRATE:
// -----------------------------------------------------------------------------
// This file should NOT contain parsing logic. Enum literals are:
// 1. Parsed as QualifiedName (e.g., Color::Red) - see path.cpp
// 2. Parsed as QualifiedApply (e.g., Option::Some(x)) - see path.cpp
// 3. Resolved to EnumLiteral during name resolution (phase 3)
//
// If any parsing helpers are needed, they would be:
// - None for parsing phase
// - ResolveQualifiedForm handles enum literal creation (see 04_analysis)
//
// DEPENDENCIES:
// - EnumLiteral AST node type (for resolution output)
// - EnumPath, VariantName helpers (for path manipulation)
// - ResolveEnumPath, ResolveEnumTuple, ResolveEnumRecord (resolution functions)
// - FieldInit type (shared with record literals)
//
// REFACTORING NOTES:
// - This file may be empty or contain only AST node definitions
// - Actual enum literal parsing is covered by QualifiedName/QualifiedApply
// - Consider removing this file if no parsing logic is needed
// - The EnumLiteral AST node and its resolution belong in 04_analysis
// - During parsing, the grammar cannot distinguish enum variants from
//   static method calls or record literals without name resolution
// =============================================================================
