# Chapter 10: Permissions and Binding State

## Load When

Use for `const`, `shared`, `unique`, receiver shorthand, alias exclusivity, activity states, and permission admissibility.

## Authoring Rules

- Default permission is `const`.
- `unique` means exclusive read-write access; it does not itself mean cleanup responsibility.
- `shared` means key-mediated synchronized access; it does not itself mean cleanup responsibility.
- Use `~` for const receiver, `~%` for shared receiver, and `~!` for unique receiver.
- Permission admissibility gates calls and non-consuming arguments; it does not rewrite the caller type.

## Syntax Forms

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

## Static Semantics

`unique` may satisfy const, shared, or unique receiver requirements. `shared` may satisfy const or shared. `const` may satisfy const. A live admissible non-consuming use suspends direct use of the original unique binding.

## Runtime and Lowering Notes

Permissions do not change value layout. Shared access proceeds through the key system.

## Diagnostics

Use for invalid receiver permission, alias/exclusivity violations, inactive unique binding use, and permission admissibility failures.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Permissions/**`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `10` for permission forms, alias rules, activity states, and admissibility.
