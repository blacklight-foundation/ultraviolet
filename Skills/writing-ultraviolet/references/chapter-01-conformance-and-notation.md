# Chapter 1: Conformance and Notation

## Load When

Use for conformance terms, behavior classification, construct vocabulary, compile-time execution ordering, target assumptions, and deciding whether a result is implementation-defined, unspecified, or outside conformance.

## Authoring Rules

- Treat compile-time, static, dynamic, and lowering behavior as distinct surfaces.
- Distinguish invalid source from compiler defects using the owning judgment and diagnostic chapter.
- Preserve source when it satisfies normative static and dynamic semantics.

## Syntax Forms

This chapter mostly defines notation and construct vocabularies. Concrete syntax is owned by feature chapters and Appendix B.

## Static Semantics

Conformance judgments name the owner for parse, resolve, type, permission, provenance, layout, lowering, runtime, and diagnostic behavior.

## Runtime and Lowering Notes

Compile-time execution and translation ordering constrain when generated code, reflection, and constants become available to later steps.

## Diagnostics

Use this chapter to classify whether a behavior should produce a compile-time diagnostic, dynamic check, panic, or outside-conformance result.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Conformance/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `1` for exact behavior categories and judgment names.
