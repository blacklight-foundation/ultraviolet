# Chapter 8: Type System

## Load When

Use for type equivalence, subtyping, inference, refinements, type well-formedness, and compatibility diagnostics.

## Authoring Rules

- Write explicit types when they make authority, permission, ownership, or public API shape clearer.
- Do not assume permission-qualified types coerce by changing permission.
- Use refinement and contract forms when a rule can be checked by the language.

## Syntax Forms

```ultraviolet
internal procedure typedValuesReference() -> bool {
    let count: i32 = 0
    let values: [i32; 3] = [1, 2, 3]
    let view: [i32] = values[..]
    return count == 0 &&
        view[1usize] == 2
}
```

## Static Semantics

The type system owns equality, subtyping, inference, value/reference classification, refinement well-formedness, and compatibility checks consumed by declarations and expressions.

## Runtime and Lowering Notes

Type classification feeds layout, ABI, cleanup, dynamic dispatch, and runtime validity checks.

## Diagnostics

Use for type mismatch, failed inference, invalid refinement, incompatible permission equality, and invalid type references.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Types/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `8` for exact type judgments.
