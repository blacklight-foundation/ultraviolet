# Chapter 12: Concrete Data Types

## Load When

Use for primitive types, tuples, arrays, slices, ranges, records, enums, unions, and type aliases.

## Authoring Rules

- Use `record` for data-first values and configuration.
- Use enum variants with explicit payload syntax when payloads exist.
- Use `(T;)` and `(expr;)` for tuple singletons; a trailing comma is not a singleton.
- Use arrays `[T; n]`, slices `[T]`, and ranges for bounded iteration or matching.
- Keep type aliases meaningful and stable.

## Syntax Forms

```ultraviolet
public record Point {
    public x: i32
    public y: i32
}

public enum EnumReference {
    Idle = 0
    Count(i32) = 1
    Named {
        value: i32
    } = 2
}
```

## Static Semantics

Concrete types own field/variant uniqueness, constructor requirements, default fields, tuple arity, array lengths, slice compatibility, range bounds, union membership, and alias expansion.

## Runtime and Lowering Notes

Concrete data types feed layout, ABI, construction, pattern matching, drop traversal, and literal lowering.

## Diagnostics

Use for bad field names, missing fields, duplicate variants, invalid tuple/array/slice/range forms, union misuse, and alias cycles.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/DataTypes/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `12` for concrete data type rules and diagnostics.
