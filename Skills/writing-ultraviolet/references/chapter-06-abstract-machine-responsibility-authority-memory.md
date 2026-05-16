# Chapter 6: Abstract Machine, Responsibility, Authority, and Memory

## Load When

Use for no ambient authority, capabilities, binding state, permission runtime state, responsibility, regions, frames, provenance, dynamic scopes, memory diagnostics, observable effects, and sequence points.

## Authoring Rules

- External effects require explicit capability values, normally from `Context`.
- `move` transfers responsibility; permission qualifiers describe access, not cleanup ownership.
- Track binding state after whole moves and field moves.
- Region and frame allocation must respect provenance and lifetime order.
- Use sequence points to reason about effect order, drop order, key behavior, and binding state.

## Syntax Forms

```ultraviolet
public procedure main(context: Context) -> i32 {
    let output: Outcome<(), IoError> = context.fs~>write_stdout("hello\n")
    return if output is {
        @Value {
            0
        }
        @Error {
            1
        }
    }
}
```

```ultraviolet
region as scratch {
    let value: i32 = scratch ^ 7
}
```

## Static Semantics

Binding states include `Valid`, `Moved`, and `PartiallyMoved(F)`. Provenance includes global, stack, heap, region, and unknown categories. Shorter-lived values cannot escape into longer-lived locations.

## Runtime and Lowering Notes

Observable events include host effects, FFI effects, panic effects, drop effects, and key effects. Dynamic scopes carry cleanup lists and binding states.

## Diagnostics

Important memory diagnostics include moved/partially moved access, immutable reassignment, partial move without `unique`, destructor call misuse, immovable move, missing region, region allocation outside a region, provenance escape, and unsafe operation outside an unsafe block.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Authority/**`
- `HelloUltraviolet/Source/Reference/Lowering/CleanupDropUnwinding.uv`
- `HelloUltraviolet/Source/Reference/Statements/Region.uv`
- `HelloUltraviolet/Source/Reference/Statements/Frame.uv`

## Spec Fallback

Open `SPECIFICATION.md` sections `6.1` through `6.6` for authority, binding state, regions, provenance, dynamic runtime state, and memory diagnostics.
