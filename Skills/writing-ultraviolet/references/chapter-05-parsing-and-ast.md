# Chapter 5: Parsing and AST

## Load When

Use for parser entry points, AST form ownership, recovery, item sequencing, attribute parsing, and syntactic disambiguation.

## Authoring Rules

- Prefer canonical grammar forms from Appendix B.
- Keep attributes immediately attached to the declaration, statement, or expression they modify.
- Use explicit delimiters for complex types, contracts, attributes, and generics.

## Syntax Forms

```ultraviolet
public procedure identity<TValue>(move value: TValue) -> TValue {
    return move value
}
```

## Static Semantics

AST forms are owned by the feature chapters even when parsing is centralized. Use the owner chapter to repair semantics after parse succeeds.

## Runtime and Lowering Notes

Parsing has no runtime behavior. It shapes later resolve, type, and lowering paths.

## Diagnostics

Use parser diagnostics for malformed forms, recovery points, bad attribute placement, and invalid item sequencing.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Parsing/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `5` and Appendix C for AST ownership.
