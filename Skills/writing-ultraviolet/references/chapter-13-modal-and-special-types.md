# Chapter 13: Modal and Special Types

## Load When

Use for modal declarations, state fields, state methods, transitions, widening, strings, bytes, safe pointers, raw pointers, function types, and closure types.

## Authoring Rules

- Use `modal` when fields, behavior, or allowed operations vary by lifecycle state.
- Use `Type@State` for modal state types and literals.
- Keep transitions near the state fields they govern.
- Use safe pointer state where possible; raw pointers belong at explicit unsafe/FFI boundaries.
- Use closure dependency syntax when shared captures need to be represented.

## Syntax Forms

```ultraviolet
public modal FileSession {
    @Closed {
        public transition open() -> @Open {
            return FileSession@Open {}
        }
    }

    @Open {
        public transition close() -> @Closed {
            return FileSession@Closed {}
        }
    }
}
```

## Static Semantics

Modal states are distinct state types. State fields and methods are available only in their state. Widening moves from a state-specific view to a broader modal view where allowed.

## Runtime and Lowering Notes

Modal payload layout, pointer state, managed strings/bytes, closure captures, and function values feed cleanup and ABI behavior.

## Diagnostics

Use for duplicate states, invalid state names, unavailable state members, invalid transitions, bad pointer state, and closure/function type errors.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/ModalTypes/**`

## Spec Fallback

Open `SPECIFICATION.md` chapter `13` for modal and special type rules.
