# Chapter 2: Diagnostics

## Load When

Use for diagnostic records, source locations, spans, diagnostic code selection, ordering, rendering, and diagnostics without source spans.

## Authoring Rules

- Report the owning feature diagnostic where one exists.
- Keep source span discussion precise: source locations, token spans, and rendered messages are separate concepts.
- Preserve diagnostic ordering when comparing expected compiler output.

## Syntax Forms

Diagnostics are data records, not ordinary user syntax. User-visible attributes that influence diagnostics are covered in chapter 9.

## Static Semantics

Diagnostic code selection is owned by the construct section that defines the failing rule. Appendix A is an index, not the canonical condition.

## Runtime and Lowering Notes

Some dynamic checks lower to runtime panic or check machinery while still having compile-time diagnostic definitions for statically known cases.

## Diagnostics

Use chapter-specific diagnostic tables first, then Appendix A only as a lookup index.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Diagnostics/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `2` for source span, code selection, ordering, and rendering rules.
