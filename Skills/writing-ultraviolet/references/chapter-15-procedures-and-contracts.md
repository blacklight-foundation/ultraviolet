# Chapter 15: Procedures and Contracts

## Load When

Use for procedure declarations, methods, receivers, overloading, contract clauses, preconditions, postconditions, invariants, verification logic, behavioral subtyping, and executable entry rules.

## Authoring Rules

- Write explicit visibility where allowed.
- Write explicit return types.
- Use explicit `return` in non-unit procedures.
- Use receiver shorthand `~`, `~%`, or `~!` for methods.
- Express preconditions, postconditions, invariants, and sequencing rules in contracts when possible.

## Syntax Forms

```ultraviolet
public procedure add(left: i32, right: i32) -> i32
|: left >= 0 && right >= 0 => @result >= left
{
    return left + right
}
```

## Static Semantics

Procedures typecheck parameters, return type, body, contracts, receiver permission, overload set, and entrypoint shape. Contract predicates must use verification-valid forms.

## Runtime and Lowering Notes

Procedure calls evaluate receiver and arguments left to right at the call sequence point. Contracts may lower to checks or verification obligations depending on context.

## Diagnostics

Use for invalid signatures, missing return, receiver misuse, overload ambiguity, contract predicate errors, invariant violations, behavioral subtyping failures, and entrypoint errors.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Procedures/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `15` for procedures, methods, contracts, invariants, verification, and entry rules.
