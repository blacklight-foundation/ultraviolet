# Form Card

Use this card when source needs a declaration, expression, statement, pattern,
type, attribute, project record, or generated compile-time surface.

## Meaning

Form is the concrete thing that exists in UV source. It may define a type,
construct a value, describe a pattern, declare behavior, attach metadata, or
produce a compile-time artifact.

## Slots

- `kind`: declaration, expression, statement, pattern, type, attribute, project,
  or quote/splice form.
- `head`: keyword, path, operator, receiver, type name, variant, state marker, or
  attribute name.
- `inputs`: values, types, capabilities, predicates, nested forms, or project
  metadata consumed by the form.
- `bindings`: names introduced by the form.
- `body`: nested forms evaluated or declared inside this form.
- `output`: value, type, binding, state, effect, diagnostic, or control edge
  produced by the form.
- `envelope`: validity boundary supplied by `envelope-card.md`.
- `obligations`: return, coverage, type, ownership, layout, ABI, proof, or
  lowering rule attached to the form.

## UV Surfaces

- Top-level items: import, using, static, procedure, comptime procedure, record,
  enum, modal, class, type alias, extern block, derive target.
- Primary expressions: literals, identifiers, paths, tuples, arrays, record
  literals, closures, `if`, `loop`, blocks, compile-time forms, quoted forms,
  type literals.
- Structural forms: records, enum variants, modal states, tuple/array values,
  record literals, type forms, pattern forms.
- Invocation forms: procedure calls, method calls, capability calls, transition
  calls, builtins.
- Attribute forms: `[[dynamic]]`, layout/hosted/export/derive/reflection and
  other attribute surfaces governed by the spec.
- Project and source forms: manifests, assemblies, modules, source roots, and
  compilation units.

## Fill Order

1. Choose the form kind.
2. Choose the head.
3. Fill inputs and nested forms.
4. Record all bindings introduced by the form.
5. Attach the envelope facets that make the form valid.
6. State the output expected by downstream cards.
7. Check the obligations before writing final UV.

## Review Checks

- The form has a single clear kind and head.
- Every input is either already bound or introduced by a parent form.
- Every binding has a scope and envelope.
- The output is consumed by another card or exits through flow.
- Obligations are checked against the bundled specification.

## Spec Search Terms

- `top_level_item`
- `primary_expr`
- `statement`
- `pattern`
- `type`
- `attribute`
- `quote_expr`
- `derive_target_decl`
