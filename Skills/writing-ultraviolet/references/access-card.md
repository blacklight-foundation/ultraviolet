# Access Card

Use this card when source observes, projects, borrows, indexes, matches, imports,
or otherwise exposes an existing form.

## Meaning

Access turns an available form into a usable view, projection, binding, payload,
or selected part while preserving the envelope rules that govern that form.

## Slots

- `subject`: value, type, module path, receiver, state, source text, region,
  capability, or shared object being accessed.
- `access_head`: field, index, path, pattern, receiver, builtin view, import,
  using, or projection operation.
- `projection`: field name, tuple index, array/slice index, enum payload, modal
  state member, byte slice, or module member.
- `bindings`: names exposed by a pattern, import, parameter, or receiver.
- `result`: value, view, binding, capability, type, or branch-local data made
  available.
- `envelope`: permission, lifetime, visibility, state, key, region, or proof
  boundary that authorizes the access.
- `failure`: missing field, method, state, permission, bounds, or coverage path.

## UV Surfaces

- Field access and tuple access.
- Array and slice indexing.
- Path access through modules, imports, and `using`.
- Pattern access with `if is` and multi-case pattern branches.
- Enum payload and record-field pattern bindings.
- Modal state-specific field, method, and transition access.
- String and bytes views such as `string::slice`, `bytes::view_string`, and
  `bytes::as_slice`.
- Receiver access through method-call syntax.
- Shared/keyed access and region/provenance-sensitive access.

## Fill Order

1. Identify the subject form.
2. Select the access head.
3. Fill the projection or pattern.
4. Name any exposed bindings.
5. Attach the required envelope facets.
6. Route the result into a downstream form, change, or flow branch.

## Review Checks

- The subject is in scope and has the expected type/state.
- Access uses a surface defined by the spec for that subject.
- Runtime indexes and shared access carry the required envelope.
- Pattern bindings remain inside their branch-local scope.
- Borrowed views do not outlive the subject envelope.

## Spec Search Terms

- `postfix_expr`
- `postfix_suffix`
- `if_case_pattern`
- `pattern`
- `bytes::as_slice`
- `bytes::view_string`
- `string::slice`
- `receiver`
- `E-UNS-0102`
