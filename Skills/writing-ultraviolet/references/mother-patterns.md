# Derived Recipes

The primary cards are:

- `references/form-card.md`
- `references/access-card.md`
- `references/change-card.md`
- `references/flow-card.md`
- `references/envelope-card.md`

Use this file after the primary cards are selected. These recipes are common
card compositions, not additional roots.

## Transform

Composition:

```text
Access source within Envelope
Flow mapping steps
Form target
```

Use for conversion, migration, normalization, encoding, decoding, rendering,
lowering, and adapter bodies.

## Accumulate

Composition:

```text
Form accumulator binding
Flow repeated focus
Change accumulator
Flow exit result
```

Use for counts, summaries, buffers, collected output, reductions, and repeated
append operations.

## Remember

Composition:

```text
Envelope persistent owner
Form stored fields
Change lifecycle or update
```

Use for records with stored state, modal lifecycle state, caches, sessions,
resource wrappers, and stateful services.

## Hide

Composition:

```text
Envelope boundary
Form public surface
Access or Change private representation through approved operations
```

Use for module APIs, private fields, facades, opaque surfaces, wrappers, and FFI
boundaries.

## Effect

Composition:

```text
Envelope authority
Form invocation
Flow outcome handling
```

Use for IO, heap allocation, system/time/network primitives, device operations,
unsafe boundaries, and foreign calls.

## Coordinate

Composition:

```text
Envelope execution domain
Flow schedule or join
Access shared state
Change lifecycle or result state
```

Use for async, parallelism, GPU dispatch, keys, waits, joins, cancellation, and
ordered shared access.

## Parser

Composition:

```text
Access source bytes or tokens
Flow repeat
Flow choose
Form parsed output
Change accumulator
```

Use for lexical, CSV, JSON, config, and structured text processing.

## Resource Lifecycle

Composition:

```text
Form resource state
Envelope authority and lifetime
Change transition
Flow outcome handling
```

Use for modal resources, allocations, handles, session objects, and cleanup
protocols.
