# Guide: Modal Resource Lifecycle

## Load When

Use for state-based resources, lifecycle protocols, modal declarations, state methods, transitions, widening, modal patterns, and resource cleanup.

## Core Model

- Use `modal` when state changes which fields, methods, or operations are valid.
- Name states in lifecycle order.
- Use transitions for state changes.
- Use `Type@State` for state-specific types and literals.
- Pattern match with `@State` where the code must branch by lifecycle state.

## Source Pattern

```ultraviolet
public modal Session {
    @Closed {
        public transition open() -> @Open {
            return Session@Open {}
        }
    }

    @Open {
        public transition close() -> @Closed {
            return Session@Closed {}
        }
    }
}
```

## Common Fixes

- Replace boolean lifecycle flags with modal states when operations differ by state.
- Move state-specific fields into the state payload.
- Use `widen` only where the broader modal view is valid.
- Use `unsafe` only for lifecycle operations that cannot be expressed safely.

## Related Chapters

- `chapter-13-modal-and-special-types.md`
- `chapter-15-procedures-and-contracts.md`
- `chapter-17-patterns.md`
- `chapter-18-statements-and-blocks.md`
- `chapter-24-lowering-lifecycle-and-backend.md`
