# Chapter 0: Front Matter

## Load When

Use for language design intent, source authority, "One Correct Way", local reasoning, explicitness, static-by-default behavior, or public explanation of why UV source has a specific shape.

## Authoring Rules

- Prefer the one accepted source form for a semantic operation.
- Make authority, ownership, movement, synchronization, suspension, allocation, copying, and dynamic checks visible in local syntax.
- Prefer static checks, contracts, modal state, and typed authority boundaries.
- Preserve spec-valid source when current compiler behavior is wrong.

## Syntax Forms

This chapter sets policy rather than concrete grammar. It constrains choices across all source forms.

## Static Semantics

Design choices should make the type, capability, permission, and effect surface locally inspectable.

## Runtime and Lowering Notes

Optimizations must preserve observable behavior, authority, responsibility, synchronization, and cleanup semantics defined later in the spec.

## Diagnostics

Use owning chapters for concrete diagnostic codes. This chapter explains why diagnostics should point at the canonical accepted form.

## Reference Corpus

- `README.md`
- `AGENTS.md`
- `HelloUltraviolet/Source/Reference/Conformance/**`

## Spec Fallback

Open `references/SPECIFICATION.md` section `0.4 Language Design Contract` when explaining design rules exactly.
