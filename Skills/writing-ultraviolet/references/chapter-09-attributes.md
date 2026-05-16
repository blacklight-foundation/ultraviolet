# Chapter 9: Attributes

## Load When

Use for `[[test]]`, diagnostic attributes, layout, optimization, vendor attributes, static/dynamic attributes, memory ordering attributes, FFI/export attributes, and derive-related attributes.

## Authoring Rules

- Place attributes directly before or around the form they modify.
- Use `[[dynamic]]` only when the intended semantics are dynamic.
- Use `[[test]]` on ordinary source procedures that satisfy the test constraints.
- Use memory-order attributes with key/shared access, not as decoration.

## Syntax Forms

```ultraviolet
[[test(name: "parser accepts tuples", covers("parser@L10"))]]
public procedure parserTupleTest(context: TestContext) -> bool
|: => @result == true
{
    return true
}
```

## Static Semantics

Each attribute has an owner feature. This chapter owns generic parsing, placement, and shared attribute validation.

## Runtime and Lowering Notes

Attributes may affect layout, linking, diagnostics, test discovery, memory ordering, and FFI boundaries.

## Diagnostics

Use for unknown attributes, duplicate attributes, invalid placement, bad arguments, and feature-owned attribute errors.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Attributes/**`
- `HelloUltraviolet/Source/Reference/Conformance/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `9` and Appendix B attribute grammar for exact attribute forms.
