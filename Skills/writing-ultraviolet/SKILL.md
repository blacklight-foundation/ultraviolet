---
name: writing-ultraviolet
description: Use when writing, reviewing, debugging, repairing, or explaining Ultraviolet source through primary pattern cards backed by the bundled specification.
---

# Writing Ultraviolet

This skill turns requested behavior into Ultraviolet by filling primary pattern
cards, selecting concrete UV forms, and checking the result against the bundled
specification.

## Required Workflow

1. Classify the request into one or more primary cards:
   `references/form-card.md`, `references/access-card.md`,
   `references/change-card.md`, `references/flow-card.md`, and
   `references/envelope-card.md`.
2. Fill each selected card before writing UV source.
3. Use `references/uv-realization-index.md` to choose the concrete UV surface
   for each filled card.
4. Use `scripts/find_uv_topic.py "<request>"` to route natural-language prompt
   terms to primary cards, realization sections, and spec search terms.
5. Use `scripts/spec_search.py "<syntax-or-diagnostic>"` for exact
   specification text when a slot, grammar rule, builtin, diagnostic, or
   conformance point needs proof.
6. Generate UV from the filled cards and review the source for card coverage,
   explicit exits, envelope validity, state change, and spec anchors.

## Start Here

- `references/form-card.md` - create, declare, pattern, type, attribute, and
  expression forms.
- `references/access-card.md` - observe, project, index, match, and borrow forms.
- `references/change-card.md` - assignment, transition, allocation, ownership,
  accumulation, and lifecycle changes.
- `references/flow-card.md` - sequence, branch, repeat, return, break, async,
  and parallel control.
- `references/envelope-card.md` - scope, authority, permission, lifetime, proof,
  state, phase, domain, ABI, and lowering boundaries.
- `references/uv-complete-syntax-system-prompt.md` - standalone comprehensive
  system prompt for testing syntax-position component patterns.
- `references/mother-patterns.md` - derived recipes assembled from the primary
  cards.
- `references/uv-realization-index.md` - UV syntax scaffolds with holes for each
  card.
- `references/pattern-card-template.md` - format for adding or revising a card.
- `references/SPECIFICATION.md` - authoritative language contract.

## Tools

- `scripts/find_uv_topic.py` - maps prompt text to primary cards, realization
  anchors, and spec search terms.
- `scripts/spec_search.py` - searches the bundled specification by text or regex.
- `scripts/validate_skill.py` - validates this skill's scaffold, routing, and
  spec synchronization.
