# Chapter 17: Patterns

## Load When

Use for basic, tuple, record, enum, modal, range, and case patterns, plus exhaustiveness and reachability.

## Authoring Rules

- Use `if value is { ... }` for structured pattern cases.
- Match modal states with `@State` patterns.
- Use enum variant patterns with the same payload shape as the enum declaration.
- Keep case ordering reachable and exhaustive where required.

## Syntax Forms

```ultraviolet
public enum EnumReference {
    Idle = 0
    Count(i32) = 1
    Named {
        value: i32
    } = 2
}

internal procedure enumPayloadValue(enum_reference: EnumReference) -> i32 {
    return if enum_reference is {
        EnumReference::Idle {
            0
        }
        EnumReference::Count(value) {
            value
        }
        EnumReference::Named { value } {
            value
        }
    }
}
```

## Static Semantics

Patterns bind names, constrain scrutinee types, check payload fields, and feed exhaustiveness/reachability analysis.

## Runtime and Lowering Notes

Pattern matching lowers to tests and bindings while preserving case order and binding state semantics.

## Diagnostics

Use for invalid pattern shape, duplicate bindings, unreachable cases, non-exhaustive cases, type mismatch, and invalid range patterns.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Patterns/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `17` and Appendix B pattern grammar for exact forms.
