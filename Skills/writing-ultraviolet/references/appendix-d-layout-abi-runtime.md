# Appendix D: Layout, ABI, and Runtime Reference

## Load When

Use for cross-indexing layout, ABI, runtime declarations, cleanup, symbol, and backend ownership.

## Authoring Rules

- Use this appendix to navigate ABI/runtime questions after reading the source-level owner chapter.
- Keep ABI and runtime effects explicit in source through attributes, FFI boundaries, modal lifecycles, and safe wrappers.

## Syntax Forms

Source syntax is owned by chapters 11 through 23; this appendix indexes layout/runtime owners.

## Static Semantics

Layout and ABI validity depend on resolved, typed, permission-checked source and FFI safety rules.

## Runtime and Lowering Notes

Use for runtime symbol, drop glue, vtable, memory intrinsic, and ABI mapping questions.

## Diagnostics

Use with chapter 24 and Appendix A for layout, ABI, runtime, and backend diagnostics.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Lowering/**`
- `HelloUltraviolet/Source/Reference/FFI/**`

## Spec Fallback

Open `references/SPECIFICATION.md` Appendix D for layout, ABI, and runtime cross-indexes.
