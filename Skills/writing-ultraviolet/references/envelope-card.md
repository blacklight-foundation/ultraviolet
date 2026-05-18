# Envelope Card

Use this card for every boundary that determines whether a form, access, change,
or flow edge is valid.

## Meaning

Envelope is the validity boundary around a UV form. It combines lexical scope,
visibility, type, permission, authority, lifetime, state, proof, phase, domain,
ABI, and lowering rules.

## Slots

- `lexical`: module, block, procedure, state block, branch, loop, or quote scope.
- `visibility`: public, internal, private, imported, exported, hosted, or facade
  surface.
- `type`: concrete type, generic, associated type, class, opaque type,
  refinement, or predicate.
- `permission`: const, shared, unique, move, partial responsibility, receiver, or
  binding state.
- `authority`: context capability, IO, heap, reactor, system, time, network, GPU,
  test authority, FFI, or unsafe boundary.
- `lifetime`: region, frame, allocation, provenance, borrowed view, managed
  object, or cleanup boundary.
- `state`: modal state, lifecycle state, task state, stream state, or resource
  state.
- `proof`: contract, invariant, precondition, postcondition, refinement proof,
  static proof, dynamic verification, or diagnostic obligation.
- `phase`: runtime, compile-time, quote/splice/emit, hosted, foreign, or lowering
  phase.
- `domain`: CPU/GPU execution domain, async, parallel, key ordering, memory
  ordering, or scheduler boundary.
- `layout`: ABI, representation, mangle/export, unwind, backend, or layout rule.

## UV Surfaces

- Module, directory, import, using, visibility, and public API boundaries.
- Type, generic, class, opaque, refinement, predicate, and associated-type
  boundaries.
- Permissions, responsibility, receiver modes, and binding states.
- Regions, frames, provenance, views, managed objects, allocation, and cleanup.
- Contracts, invariants, tests, static proof, and `[[dynamic]]`.
- Attributes, compile-time forms, derive/reflection/emit, and phase ordering.
- Capabilities, context authority, heap authority, unsafe, hosted, and FFI.
- Async, parallel, GPU/domain dispatch, keys, shared access, and memory ordering.
- Runtime, lowering, ABI, layout, and backend constraints.

## Fill Order

1. Identify the form or flow edge governed by the envelope.
2. Fill only the facets that affect validity.
3. Attach exact authority and permission to the smallest owning surface.
4. Attach proof obligations where the language can express them.
5. State lifetime and state boundaries for borrowed, managed, modal, or resource
   forms.
6. State phase, domain, ABI, and lowering boundaries when the surface crosses
   those systems.

## Review Checks

- Authority is explicit in a parameter, receiver, context, or boundary form.
- Permission and responsibility match every access or change.
- Borrowed values remain within their lifetime envelope.
- Modal operations are available in the current state envelope.
- Contracts, refinements, and invariants are expressed on the owning form.
- Dynamic verification is scoped to the smallest runtime-verified envelope.
- FFI, unsafe, layout, and backend boundaries are isolated and documented.

## Spec Search Terms

- `visibility`
- `permission`
- `responsibility`
- `authority`
- `capability`
- `region`
- `frame`
- `provenance`
- `contract`
- `invariant`
- `refinement`
- `[[dynamic]]`
- `unsafe`
- `ffi`
- `execution domain`
- `key`
