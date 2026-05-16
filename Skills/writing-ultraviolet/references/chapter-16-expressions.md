# Chapter 16: Expressions

## Load When

Use for literals, names, access/place expressions, calls, operators, casts, transmutes, construction, control expressions, effectful core expressions, closures, and pipelines.

## Authoring Rules

- Use `.` for runtime field and tuple access.
- Use `::` for module, type, enum variant, static, and associated qualification.
- Use `~>` for method calls.
- Use explicit `move` when consuming a place.
- Use `widen` for modal/type widening where allowed.
- Keep unsafe operations inside `unsafe { ... }`.

## Syntax Forms

```ultraviolet
public record ExpressionPlaceReference {
    public first: i32
    public second: i32
}

public enum EnumReference {
    Idle = 0
    Count(i32) = 1
}

internal type Payload = i32

internal procedure consume(move payload: Payload) -> Payload {
    return move payload
}

internal procedure expressionForms() -> bool {
    let record_value: ExpressionPlaceReference = ExpressionPlaceReference {
        first: 4,
        second: 9
    }
    let value: i32 = record_value.second
    let enum_value: EnumReference = EnumReference::Count(3)
    let token: CancelToken@Active = CancelToken::new()
    let child: CancelToken@Active = token~>child()
    let payload: Payload = 7
    let consumed: Payload = consume(move payload)
    return value == 9 &&
        consumed == 7 &&
        !child~>is_cancelled() &&
        if enum_value is {
            EnumReference::Idle {
                false
            }
            EnumReference::Count(count) {
                count == 3
            }
        }
}
```

## Static Semantics

Expressions resolve names, classify places, check move/borrow/access permissions, infer types, validate callee and arguments, and enforce effectful construct rules.

## Runtime and Lowering Notes

Evaluation order is left to right within sequence-point segments. Construction, calls, closures, casts, transmutes, and effectful forms lower through chapter-specific owner rules.

## Diagnostics

Use for unresolved names, invalid access, bad method/static call, argument mismatch, invalid move, unsafe outside block, cast/transmute errors, construction errors, and closure capture violations.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Expressions/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `16` and Appendix B expression grammar for exact expression forms.
