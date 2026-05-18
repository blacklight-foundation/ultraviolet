# Change Card

Use this card when source updates a value, ownership state, modal state,
accumulator, resource lifecycle, allocation state, or synchronization state.

## Meaning

Change moves a form from one valid state to another. The target, operation, and
next state are governed by the current envelope and the envelope produced by the
change.

## Slots

- `target`: variable, field, accumulator, modal state, resource, binding,
  allocation, key-protected value, or responsibility token being changed.
- `current`: current type, permission, state, lifetime, region, proof, or domain.
- `operation`: assignment, transition, append, allocation, move, consume, release,
  synchronization, or foreign effect.
- `inputs`: values, capabilities, heap/region authority, keys, predicates, or
  nested forms required by the operation.
- `next`: updated value, next modal state, changed permission, new lifetime,
  accumulated result, resource state, or error outcome.
- `envelope_delta`: permission, lifetime, proof, authority, state, or domain
  change caused by the operation.

## UV Surfaces

- `var` updates and assignment expressions.
- Accumulator updates inside loops.
- Modal transitions and state-specific methods.
- Managed string and bytes construction or append operations.
- Allocation through heap, region, frame, or runtime authority.
- Ownership, move, partial responsibility, shared/unique/const permission
  changes.
- Key and memory-ordering state changes.
- Async, parallel, task, stream, and cancellation lifecycle changes.
- FFI and unsafe operations wrapped by a safe envelope.

## Fill Order

1. Identify the target being changed.
2. State the current envelope.
3. Choose the operation.
4. Fill required inputs and authority.
5. State the next value or state.
6. Record the envelope delta.
7. Route success and failure outcomes through flow.

## Review Checks

- The target is mutable or transition-capable in the current envelope.
- The operation has the required authority and permission.
- The next state is represented in the type, modal state, or binding envelope.
- Accumulator and lifecycle changes happen in the smallest useful scope.
- Cleanup, release, or failure behavior is visible in flow.

## Spec Search Terms

- `assignment`
- `transition`
- `modal_decl`
- `state_block`
- `permission`
- `responsibility`
- `HeapAllocator`
- `region`
- `frame`
- `key`
