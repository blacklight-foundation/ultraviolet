# Guide: Procedures, Contracts, Tests, and Verification

## Load When

Use for procedure signatures, methods, receivers, `main`, contracts, preconditions, postconditions, invariants, verification logic, and `[[test]]`.

## Core Model

- Procedures use explicit return types.
- Non-unit procedures use explicit `return`.
- Public declarations use explicit documentation and visibility in project code.
- Contracts encode machine-checkable rules.
- `[[test]]` marks ordinary source procedures and requires a postcondition-bearing contract.

## Source Patterns

```ultraviolet
public procedure add(left: i32, right: i32) -> i32
|: left >= 0 && right >= 0 => @result >= left
{
    return left + right
}
```

```ultraviolet
[[test(name: "addition is monotone", covers("math@L1"))]]
public procedure additionTest(context: TestContext) -> bool
|: => @result == true
{
    return add(1, 2) == 3
}
```

## Common Fixes

- Add missing return type.
- Add explicit `return` in non-unit bodies.
- Put receiver shorthand in method parameter position.
- Add a postcondition to `[[test]]` procedures.
- Keep contract expressions pure and verification-valid.

## Related Chapters

- `chapter-09-attributes.md`
- `chapter-15-procedures-and-contracts.md`
- `chapter-22-compile-time-execution.md`
- `chapter-02-diagnostics.md`
