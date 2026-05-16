# Guide: Memory, Responsibility, and Permissions

## Load When

Use for ownership-style questions, `move`, partial moves, cleanup responsibility, binding state, `const`, `shared`, `unique`, receiver permissions, and moved-value diagnostics.

## Core Model

- `move` transfers responsibility for a value.
- Permission qualifiers describe access: `const`, `shared`, `unique`.
- Responsibility and permission are separate facts.
- A binding can be `Valid`, `Moved`, or `PartiallyMoved(F)`.
- Responsible bindings register cleanup. Moved bindings skip cleanup. Partially moved aggregates clean up the remaining children.
- `let` and `var` affect rebinding; they do not replace permission or responsibility.

## Permissions

| Permission | Read | Write | Aliasing | Synchronization |
| --- | --- | --- | --- | --- |
| `const` | yes | no | unlimited | none |
| `shared` | yes | yes | aliasable | key-mediated |
| `unique` | yes | yes | exclusive | none |

Receiver shorthand:

- `~` means const receiver.
- `~%` means shared receiver.
- `~!` means unique receiver.

Admissibility:

- `const` may call `~`.
- `shared` may call `~` and `~%`.
- `unique` may call `~`, `~%`, and `~!`.
- Admissibility does not rewrite the caller type and does not create an alias.

## Source Patterns

```ultraviolet
internal record PermissionCell {
    internal value: i32

    internal procedure readConst(~) -> i32 {
        return self.value
    }

    internal procedure readShared(~%) -> i32 {
        return self.value
    }

    internal procedure readUnique(~!) -> i32 {
        return self.value
    }
}
```

```ultraviolet
internal procedure consumeCell(move cell: unique PermissionCell) -> i32 {
    let consumed: unique PermissionCell = move cell
    return consumed.value + 0
}
```

## Common Fixes

- Add explicit `move` when consuming a unique place.
- Do not read a whole binding after moving it.
- After moving a field, avoid reading that moved field; clean up or reassign as required.
- Use a key block before reading or writing `shared` state.
- Use a receiver permission that matches the operation: `~` for read-only, `~%` for shared mutation, `~!` for exclusive mutation.

## Related Chapters

- `chapter-06-abstract-machine-responsibility-authority-memory.md`
- `chapter-10-permissions-and-binding-state.md`
- `chapter-18-statements-and-blocks.md`
- `chapter-24-lowering-lifecycle-and-backend.md`
