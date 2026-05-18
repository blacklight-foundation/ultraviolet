# Flow Card

Use this card when source orders forms, selects branches, repeats behavior,
exits a body, joins work, waits, yields, or coordinates execution.

## Meaning

Flow defines how control and evaluation move between forms. It owns sequencing,
selection, repetition, exits, async/parallel coordination, and result routing.

## Slots

- `entry`: form or envelope where flow begins.
- `steps`: ordered forms evaluated in a block or procedure body.
- `selection`: condition, pattern subject, outcome, state, or predicate used to
  select a branch.
- `iteration`: loop condition, focus pattern, source, progress rule, and
  invariant.
- `coordination`: async, parallel, dispatch, wait, join, cancellation, key, or
  domain rule.
- `exit`: return, break, branch expression, yield, panic path, or joined result.
- `result`: value, state, effect, or diagnostic delivered to the next envelope.

## UV Surfaces

- Blocks and statement sequencing.
- Procedure bodies and explicit return.
- `if`, `if is`, and multi-case pattern choice.
- Predicate loops, iterator loops, and loop expressions with `break value`.
- Error/outcome handling through branch flow.
- Async, stream, wait, yield, race/all, cancellation, and resume surfaces.
- Structured parallelism, GPU dispatch, ordered reductions, and joins.
- Compile-time quote/splice/emit ordering where phase flow matters.

## Fill Order

1. Identify the entry form.
2. List the ordered steps.
3. Fill selection or iteration slots when flow branches or repeats.
4. State progress for repeated flow.
5. State join/cancellation rules for coordinated flow.
6. Fill the exit edge for every non-`unit` path.
7. Verify the result shape of every branch or loop expression.

## Review Checks

- Every non-`unit` procedure has a procedure-level explicit return.
- Branches produce compatible result shapes.
- Repetition has visible progress.
- Loop invariants state the preserved rule when expressible.
- Async/parallel joins and cancellation paths are explicit.
- Exits return values that satisfy the current envelope.

## Spec Search Terms

- `block_expr`
- `statement`
- `if_expr`
- `loop_expr`
- `loop_condition`
- `loop_invariant`
- `return`
- `break`
- `async`
- `parallel`
- `dispatch`
