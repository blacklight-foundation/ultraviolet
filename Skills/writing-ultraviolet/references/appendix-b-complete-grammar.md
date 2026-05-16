# Appendix B: Complete Grammar

## Load When

Use for exact concrete syntax when writing or repairing source.

## Authoring Rules

- Generic parameter lists use semicolons.
- Generic argument lists use commas.
- Tuple singleton syntax uses semicolon.
- Method calls use `~>`.
- Runtime access uses `.`, while module/type/static qualification uses `::`.

## Syntax Forms

```ultraviolet
internal type Pair<TFirst; TSecond> = (TFirst, TSecond)

internal record GrammarObject {
    internal field: i32
}

internal procedure grammarForms() -> bool {
    let one: (i32;) = (1;)
    let object: GrammarObject = GrammarObject { field: 3 }
    let value: i32 = object.field
    let token: CancelToken@Active = CancelToken::new()
    let child: CancelToken@Active = token~>child()
    return one.0 == 1 &&
        value == 3 &&
        !child~>is_cancelled()
}
```

## Static Semantics

Grammar acceptance is necessary but not sufficient; use the owning chapter for semantics.

## Runtime and Lowering Notes

Grammar has no runtime behavior.

## Diagnostics

Use parser diagnostics for malformed grammar, then owner chapters for semantic failures.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Parsing/**`
- All `HelloUltraviolet/Source/Reference/**` source specimens.

## Spec Fallback

Open `references/SPECIFICATION.md` Appendix B for exact grammar.
