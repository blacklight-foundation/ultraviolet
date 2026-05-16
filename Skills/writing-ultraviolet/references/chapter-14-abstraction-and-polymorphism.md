# Chapter 14: Abstraction and Polymorphism

## Load When

Use for generic parameters and arguments, generic procedures/types, classes, implementations, associated types, dynamic class objects, opaque types, refinement types, capability classes, and foundational predicates.

## Authoring Rules

- Separate generic parameters with semicolons: `<TValue; TState>`.
- Separate generic arguments with commas: `<i32, bool>`.
- Use predicate clauses for semantic requirements such as `Bitcopy`, `Clone`, `Drop`, and `FfiSafe`.
- Use classes for behavior contracts; use implementations to satisfy them.
- Use dynamic class objects only when dynamic dispatch is semantically intended.

## Syntax Forms

```ultraviolet
internal procedure pairGeneric<TFirst; TSecond>(
    first: TFirst,
    second: TSecond
) -> (TFirst, TSecond)
|: Bitcopy(TFirst)
Bitcopy(TSecond)
{
    return (first, second)
}
```

## Static Semantics

Generic parameters have bounds and predicate requirements. Implementations must satisfy class members and associated type obligations. Dynamic class object use is constrained by method eligibility and permission rules.

## Runtime and Lowering Notes

Generics may monomorphize or lower through implementation dictionaries. Dynamic class objects lower through vtable-like runtime data and interact with drop and ABI rules.

## Diagnostics

Use for malformed generic lists, unsatisfied predicates, duplicate implementations, missing class members, associated type errors, invalid opaque exposure, and dynamic object misuse.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Polymorphism/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `14` for abstraction, polymorphism, class, and predicate rules.
