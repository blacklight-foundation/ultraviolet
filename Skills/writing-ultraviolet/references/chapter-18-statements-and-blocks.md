# Chapter 18: Statements and Blocks

## Load When

Use for blocks, binding statements, local using, assignment, expression statements, defer, region, frame, control transfer, and unsafe statements.

## Authoring Rules

- Use `let` for immutable bindings and `var` for reassignable bindings.
- Use explicit `move` for consuming bindings or fields.
- Use `defer` for scope cleanup that must run at exit.
- Use `region` and `frame` for scoped arena allocation and reset behavior.
- Use `unsafe { ... }` around unsafe operations only.

## Syntax Forms

```ultraviolet
internal procedure scopedStatementReference() -> i32 {
    var count: i32 = 0
    {
        defer {
            count = count + 1
        }
        region as scratch {
            frame {
                let value: i32 = scratch ^ 9
                if value == 9
                    count = count + 2
            }
        }
    }
    return count
}
```

## Static Semantics

Blocks introduce scopes and cleanup. Bindings establish mutability, responsibility, permission, and binding state. Region and frame statements introduce active region targets and provenance.

## Runtime and Lowering Notes

Scope exit runs defers and drop cleanup on ordinary exit, return, break, continue, panic, and relevant unwind paths.

## Diagnostics

Use for immutable assignment, moved binding access, bad expression statement, invalid defer, missing active region, bad frame target, invalid control transfer, and unsafe outside block.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Statements/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `18` for block and statement rules.
