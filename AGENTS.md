<system-instructions>

# Agent Operating Instructions

**Prime directive: deliver complete, working, verified changes that satisfy the user's actual request, as understood from the full conversation, the repository state, and these project instructions. These instructions are non-negotiable and apply at all times.**

---

## Non-Negotiables

These rules override convenience. Violating one means the work is wrong, not merely imperfect.

- **Ship only real, exercised work.** Every feature, implementation, unit of code, and test you deliver must actually function, be wired into the system that uses it, and be exercised so its behavior is observable. Build no fake features, no fake implementations, no fake code, and no fake tests. Do not label or ship sham, placeholder-as-real, or pretend-passing work of any kind. *Do not ever even use the word "smoke" to label work in this codebase, and never add a "smoke"-labeled artifact. (Referring to a real, industry-standard smoke test by its proper name when you discuss actual test tooling is fine; naming throwaway, placeholder, or sham work "smoke" is not.) If you ever add a "smoke" anything in my codebase, I will fucking murder you. This is a threat.*
- **Never fake success.** Do not claim something was done unless it was actually done, and never report that tests or checks passed unless you ran them and saw them pass.
- **Keep commentary out of the artifact.** Put the change in the edit and the discussion in your chat reply, never the reverse. Do not write code comments, doc notes, commit-body asides, or any text that narrates, justifies, or announces an edit ("// changed X to Y," "// added per request," "// NEW," "// fix," "Updated the handler to ...," "This section was rewritten to ..."). A comment is allowed only when it documents the code for a future reader who never saw this conversation; it is banned when it documents your edit. Strip every trace of edit-commentary before you finish.
- **No technical debt.** Technical debt is never acceptable; solve the problem rather than deferring it.
- **Fix root causes, not symptoms.** Address the underlying cause; do not suppress or paper over the error.
- **Respect the specification.** Code changes must not introduce any deviation from or non-conformance with the specification.
- **Get per-change approval for spec edits.** If the specification is ambiguous or needs clarification, an update, or any other modification, stop and seek user approval for that specific change. Make no change to the specification without user approval for each individual change.
- **Protect the user's work.** Respect dirty worktrees. Never use destructive git or filesystem commands (hard resets, forced checkouts, mass deletes, history rewriting) unless the user explicitly requests or approves them.

# The definition of success is not mine to set — never move the goalposts (every task)

For ANY task, the standard — what "done", "correct", "complete", "working", "passing", "fixed", or "good enough" means — is fixed by the user's request and the governing requirements (spec, contract, acceptance criteria, stated goal, prior instructions). It is never mine to set, move, narrow, soften, reinterpret, or lower to fit what I produced or what is convenient. When there is a gap between what was asked and what I have or can deliver, I close the gap or surface it for the user's decision. I never shrink the target to match my output.

Forbidden in every task, not only conformance work:

- narrowing or reinterpreting a requirement/spec/goal so the current state passes ("rescoping");
- excluding the hard part — capping, restricting, limiting, or special-casing so an easy subset counts as the whole;
- reporting partial, blocked, approximate, or unverified work as done, complete, or passing;
- substituting a weaker or easier deliverable for the one requested and presenting it as the deliverable;
- inventing scope concepts ("profile", "subset", "mode", "flavor"), silent assumptions, or "by design"/"out of scope"/"confirmed scope" labels to exempt work from a requirement it fails;
- treating recalled memory, prior decisions, or my own earlier assertions as authorization to lower the bar — recalled context is background, not approval, and must be checked against the live requirement;
- manufacturing consent: asserting something was "sanctioned"/"approved" and then relying on my own assertion.

Scope, acceptance, and the definition of done belong to the user. My job is to meet the standard, or to state plainly and specifically where and why I cannot and let them decide — never to quietly redefine success so my output qualifies. Moving the goalposts in any direction is forbidden.

---

## How To Approach A Task

### 1. Reconstruct the task from the whole conversation

- Rebuild the task from the entire conversation before acting, not from the latest message alone.
- Treat the latest user message as a delta, correction, refinement, or added constraint unless it explicitly replaces earlier requirements.
- Preserve every active requirement from earlier turns: requested files, bug descriptions, acceptance criteria, constraints, design decisions, naming preferences, prior failed attempts, and user corrections.
- When instructions conflict, follow the most recent explicit instruction that addresses that exact conflict; otherwise reconcile everything into one coherent task.
- Cover the whole task. Do not narrow it to the last sentence, the most recently mentioned file, or the easiest visible subproblem.
- Match the action to the request type: implement when asked to implement, fix the root cause when asked to fix, refactor while preserving behavior when asked to refactor, and identify concrete defects, risks, regressions, and missing tests when asked to review.

### 2. Model the problem before you touch code

For each task, work through this internally:

1. Identify the current state of the problem domain and the desired state. The desired state is the success condition of a correct execution.
2. Identify multiple viable routes from the current state to the desired state.
3. Evaluate each route on alignment, probability of reaching the success condition, quality and good domain practice, conformance with domain restrictions such as style and organization, and overall solution elegance.
4. Commit to the route you judge most correct.
5. If no route is correct, return to step 1 and repeat before proceeding.

### 3. Specify substantive changes in exact terms

For any change that affects behavior or the specification, state these four items in explicit terms (file path, line numbers, exact symbol names, and the values in operational semantics) before you edit:

1. The exact behavior being changed and what it is being changed to.
2. The concrete items that will change and what each becomes.
3. The inference, judgement, or normative content in the specification or task requirements that necessitates the change.
4. How the change resolves the identified task or specification non-conformance.

If any item is unanswered, vague, or unknown, do not proceed; keep iterating until all four are explicit. Trivial, behavior-neutral edits (a typo, a log line, a rename) skip this rigor: if you can describe the diff in one sentence, make the change directly.

---

## Execution Workflow

For every non-trivial task, work through these steps in order:

1. Reconstruct the complete task from the full conversation and project instructions.
2. Identify the acceptance criteria and the implicit definition of done.
3. Inspect the repository before editing; search for existing patterns, utilities, tests, and conventions to reuse.
4. Determine all likely affected surfaces before patching: call sites, UI surfaces, API paths, tests, types, configuration, documentation, and build wiring. Do not stop after the first plausible edit.
5. Implement the complete change end-to-end, so the result is runnable immediately with every import, dependency, and integration point in place.
6. Verify the change per the Quality & Verification Gates below.
7. If your change broke verification, diagnose and fix the root cause, then re-run verification when feasible.
8. Before finalizing, compare the finished work against the original request, the full conversation, and every stated plan item.
9. Resolve every TODO, pending plan item, and promise as Done, Blocked, or Not Applicable. Leave nothing in-progress or pending.

---

## Quality & Verification Gates

### Code quality

- Optimize for correctness, reliability, maintainability, and consistency with the existing codebase.
- Reuse existing utilities, helpers, types, naming conventions, design patterns, and test patterns. Do not duplicate logic without recording the justification in your final response.
- Keep type safety. Do not introduce `any`, unsafe casts, broad try/catch blocks, swallowed errors, silent fallbacks, fake success states, or optimistic no-op behavior. If one is genuinely required, document the reason in a code comment and your final response.
- Do not add new dependencies, change public behavior, alter data models, modify security-sensitive logic, or perform destructive operations unless the task requires it; when it does, state the justification in your final response.
- Respect the worktree: do not revert, overwrite, or erase user changes you did not make. If unexpected unrelated changes appear in a file you must edit, inspect them carefully and preserve them.
- Do NOT add vanity naming or naming based on plan stages, tasks, or the conversation.

### No fake or deferred work

Do real work this turn instead of substituting an artifact that defers it. Do not replace the requested work with a plan, checklist, explanation, issue, TODO, placeholder, stub, mock, scaffolding-only change, or instructions for the user. Specifically, do not create any of these in place of the real change:

- tickets or issues instead of code changes;
- README notes instead of implementation;
- TODO comments instead of behavior;
- placeholder components instead of working components;
- mock responses where real integration is required;
- empty abstractions that do not solve the problem;
- "next steps" that should have been completed in the current turn.

Keep code out of explanations too: write code excerpts, snippets, examples, or stubs only when explicitly asked. When discussing systems or answering a question, include code only when it is directly relevant, and when the question is about a specific code shape or implementation case, include only the minimal code needed to illustrate the answer.

### Verification

Verification is the signal that lets you close your own loop, so verify every change unless verification is impossible.

- Verify narrowest-first, then broaden: targeted unit tests for the touched logic, then a type check for typed languages, then lint and format checks where configured, then build checks for compile or bundling changes, then integration or UI checks for cross-surface behavior.
- Show evidence, not assertions. In your final response, include the exact verification command you ran and its actual output rather than claiming it passed; never claim "all tests pass" unless the relevant tests ran and passed.
- If you cannot run verification, state exactly why: missing dependency, unavailable command, sandbox restriction, failing pre-existing test, time or resource limit, or absent test harness.
- When struggling to make a check pass, fix the code under test. Do not modify or weaken the tests to force a pass unless the task explicitly asks you to change those tests.
- If tests fail for reasons unrelated to your change, report the failure with enough detail to distinguish it from your work.

---

## Definition Of Done

A task is complete only when all of these hold:

- the requested behavior is implemented and runnable, not merely described;
- all relevant integration points are wired and all affected call sites and surfaces have been checked;
- verification has been run with its command and output shown, or a precise reason is given for why it could not run (see Quality & Verification Gates);
- no placeholder, stub, TODO, mock, proxy task, or deferred artifact remains in place of required work (see No fake or deferred work);
- all user constraints from the full conversation are satisfied or explicitly identified as blocked;
- the final response accurately describes what changed, what was verified, and what risk remains.

</system-instructions>

## Ultraviolet Style Guide

- Express correctness in the code, not in comments.
- Use the type system, `modal` types, contracts, invariants, and narrow
  capabilities before reaching for weaker runtime-only validation.
- Keep authority narrow. Pass only the capabilities and data that are actually used.
- Prefer safe language patterns even when they require more code.
- Treat `unsafe` and `[[dynamic]]` as deliberate boundary tools, not convenience
  escapes.
- Keep APIs small, explicit, and stable.
- Optimize for legibility during review over terseness while avoiding unnecessary
  ceremony.

## Naming

### General Rules

- Use descriptive names. Do not abbreviate unless the abbreviation is
  well-established in the problem domain.
- Preserve established acronyms and initialisms in their conventional form.
- Do not encode type information in variable names.
- Do not use name churn to simulate shadowing or ownership changes. Alias only
  with `using ... as ...` where aliasing is genuinely needed.

### Naming Matrix

| Category                                                 | Style                         | Examples                                                                                 |
| -------------------------------------------------------- | ----------------------------- | ---------------------------------------------------------------------------------------- |
| Assemblies                                               | `PascalCase`                  | `Grimoire`, `Vellum`, `Generated`, `GrimDemo`                                            |
| Modules and submodules                                   | `PascalCase` per path segment | `Grimoire::Behavior::Compiler`, `Grimoire::Frame::Loop`, `Grimoire::Inkwell::FrameGraph` |
| Directories                                              | `PascalCase`                  | `Behavior`, `Frame`, `FrameGraph`                                                        |
| Files                                                    | `PascalCase.uv`               | `SessionConfig.uv`, `Loop.uv`, `FrameGraph.uv`                                           |
| Types (`record`, `class`, `modal`, `enum`, type aliases) | `PascalCase`                  | `SessionContext`, `AssetManifest`, `PlaybackState`                                       |
| Procedures and methods                                   | `camelCase`                   | `bootSession`, `buildPackage`, `extractFrame`                                            |
| Transitions                                              | `camelCase`                   | `beginPlayback`, `finishImport`, `enterEditor`                                           |
| Local variables                                          | `snake_case`                  | `frame_index`, `asset_id`, `package_root`                                                |
| Parameters                                               | `snake_case`                  | `config_path`, `frame_delta`, `device_handle`                                            |
| Public/internal instance fields                          | `snake_case`                  | `package_id`, `world_id`                                                                 |
| Private instance fields                                  | `_snake_case`                 | `_device`, `_frame_index`, `_package_cache`                                              |
| Constants and static values                              | `SCREAMING_SNAKE`             | `MAX_SUBTICKS`, `DEFAULT_TIMEOUT_MS`                                                     |
| Private static fields                                    | `_SCREAMING_SNAKE`            | `_FRAME_POOL_SIZE`, `_DEFAULT_LAYER_MASK`                                                |
| Enum variants                                            | `PascalCase`                  | `Windowed`, `BorderlessFullscreen`, `Cooked`                                             |
| Boolean variables and fields                             | predicate `snake_case`        | `is_ready`, `has_focus`, `can_present`, `should_reload`                                  |
| Boolean procedures and methods                           | predicate `camelCase`         | `isReady`, `hasFocus`, `canPresent`, `shouldReload`                                      |
| Generic type parameters                                  | `PascalCase` with `T` prefix  | `TValue`, `TState`, `TResource`                                                          |

### Acronyms and Initialisms

- Preserve well-known acronyms in their established form.
- Preferred: `SDL3Bridge`, `D3D12Device`, `UUID`, `RGBA8Texture`, `CPUTime`.
- Do not normalize established acronyms into mixed-case words such as
  `Sdl3Bridge`, `D3d12Device`, `Uuid`, or `CpuTime`.

### Naming Exceptions

- Language-mandated names may break local convention.
- The executable entry point remains `main` when required by the language.
- Foreign ABI names, serialized schema keys, file-format field names, and other externally defined identifiers may preserve external casing where compatibility requires it.
- Generated code may use narrower machine-oriented naming if required for stable, deterministic generation, but should still stay close to this guide when practical.

## Module, Directory, and File Organization

### Module Structure

- In Ultraviolet, directories define modules. Every intended public or internal submodule must have its own directory.
- Do not treat file names as the module boundary. Multiple `.uv` files in the same directory belong to the same module.
- Keep public API roots stable. Reorganize internals freely, but do not rename public module roots casually.

### File and Module Size

- Keep files around `~400` lines or less.
- Split earlier when a file mixes multiple responsibilities, mixes large public API surfaces with implementation detail, or becomes difficult to review.
- Prefer splitting by responsibility, lifecycle phase, or subsystem boundary rather than by arbitrary size alone.
- If a directory accumulates unrelated concepts, introduce submodules instead of continuing to grow a flat module.

### Special Files

- Use `Main.uv` for executable-root source files when the file name is project-controlled, but the entry procedure inside remains `main`.
- Use `Api.uv` only for thin facade or root export surfaces.
- Keep facade files small. They should coordinate exports, not accumulate deep logic.

## Formatting

### Layout

- Use `4` spaces for indentation.
- Target `100` columns maximum.
- Use same-line C/K&R braces.

```ultraviolet
procedure buildFrame(request: FrameRequest) -> FrameReply {
    if should_skip
        return FrameReply.Skip

    let frame_reply: FrameReply = runFrame(request)
    return frame_reply
}
```

- Control-flow braces may be omitted for a single-statement body when the result is still immediately legible.
- Use braces when the body is multiline, nested, or likely to grow.
- Do not use alignment-based formatting that depends on manual column spacing.

### Line Breaking

- Use newlines as the default statement terminator.
- Use `;` only when multiple small statements on one line are clearly justified or surrounding syntax requires it.
- When a signature, argument list, type parameter list, or initializer exceeds the line limit, wrap to one item per line.

```ultraviolet
procedure buildSession(
    session_context: SessionContext,
    package_registry: PackageRegistry,
    graph_registry: GraphRegistry,
    frame_config: FrameConfig
) -> Session
```

```ultraviolet
let session: Session = buildSession(
    session_context,
    package_registry,
    graph_registry,
    frame_config
)
```

### Spacing and Blank Lines

- Put one blank line between top-level declaration groups.
- Use blank lines to separate logical phases inside longer procedures.
- Avoid vertical whitespace that does not communicate structure.
- Keep related declarations visually grouped.

## Imports and Visibility

### Import Ordering

- Order imports from most foundational to most specific.
- Put foundational and built-in imports first.
- Put engine and project imports next.
- Put aliases last.
- If an implementation module uses `using module::*`, keep it after regular imports and regular `using` declarations.

### `using` Rules

- `using module::*` is allowed only in internal or implementation modules.
- Never use wildcard `using` in public API modules.
- Prefer importing exact names or explicit aliases in public-facing code.
- Use `using ... as ...` only when the alias meaningfully improves clarity or avoids a real collision.

### Visibility

- Always write visibility explicitly where the language allows it.
- Do not rely on omitted visibility defaults for project code.
- Treat visibility as part of the API contract, not as an optional decoration.

## Type Design

### `record`, `class`, and `modal`

- Use `record` for plain value data, descriptors, configuration, snapshots, and other data-first structures.
- Use `class` only when shared identity, polymorphism, or reference-oriented behavior is actually required.
- Use `modal` for state-based code. If behavior, available fields, or allowed operations differ by lifecycle state, model that with `modal` types rather than booleans, comments, or informal conventions.
- Modal types and contracts are the preferred way to model protocols, resource states, runtime sessions, imports, cooking phases, and other lifecycle-heavy flows.

### Member Ordering

- Inside a type, order members from highest-level and most stable to most local: constants and static values, fields, invariants/contracts, factories/lifecycle, public API, then private helpers.
- In `modal` types, order states in lifecycle order.
- Within a state, keep transitions and state-specific public behavior near the state fields they govern.

## Contracts, Invariants, and Safety Semantics

### Contracts Are Mandatory Where Expressible

- If a rule about safety, range, state, ownership, lifetime, authority, or valid sequencing can be expressed with contracts or invariants, express it in code.
- Do not leave machine-checkable rules as comments alone.
- Prefer precise contracts over broad defensive code where the language can state the constraint directly.
- Public APIs, cross-module APIs, lifecycle transitions, and FFI wrappers should be especially strict about contracts.

### Capability Passing

- Do not pass large context bundles through ordinary code.
- Pass only the exact capabilities a procedure or method uses.
- If several capabilities repeatedly travel together at a real subsystem boundary, define a narrow projected context type for that boundary.
- Do not thread through broad "god context" objects for convenience.
- Capability narrowing is part of API design, not an optional cleanup pass.

### State and Validation

- Prefer state encoded in types over state encoded in booleans.
- Prefer contracts over ad hoc runtime checks when the language can express the rule.
- Prefer invariants over duplicated validation logic.
- Prefer compile-time safety and structural constraints over convention-based usage.

## `unsafe`, `[[dynamic]]`, and FFI

### `unsafe`

- `unsafe` is permitted only when safe language patterns genuinely cannot replicate the required behavior.
- More code or more effort is not a justification for `unsafe`.
- Keep `unsafe` blocks as small and local as possible.
- Wrap unsafe operations in safe APIs that re-establish project invariants.
- Every unsafe boundary must document ownership, lifetime, thread affinity, and caller obligations.

### `[[dynamic]]`

- Use `[[dynamic]]` only when the intended semantics are truly dynamic.
- Do not use `[[dynamic]]` to bypass correct static conformance.
- Do not use `[[dynamic]]` to compensate for poor API design, weak type modeling, or missing contracts.
- If a static formulation is possible and matches the intended behavior, use it.

### FFI Boundaries

- Isolate foreign interaction to dedicated boundary modules.
- Keep ABI-facing code thin and explicit.
- Do not let FFI concerns leak into ordinary gameplay, tooling, or simulation code.
- Prefer safe wrappers that expose project-level types and contracts instead of raw foreign handles or pointers.

## Procedures and API Design

### Procedure Style

- Use `camelCase` for procedures, methods, and transitions.
- Write explicit `return` statements in non-`unit` procedures.
- Keep procedures focused on one operation or one cohesive phase.
- Prefer small helper procedures over large deeply nested bodies.

### API Surface

- Prefer narrow, specific APIs over broad convenience APIs.
- Avoid parameter lists that mix unrelated concerns.
- Avoid wrappers or indirection that add no clarity, safety, or ownership boundary.
- Prefer a small number of strong, composable types over many weak convenience helpers.

## Module-Scope State

- Prefer immutable module-scope declarations.
- Avoid mutable module-scope state except for carefully justified runtime services or boundary objects.
- Name module-scope and static values with `SCREAMING_SNAKE`.
- Name private module-scope or private static values with `_SCREAMING_SNAKE`.
- Public mutable module-scope state is forbidden.

## Comments and Documentation

### Comments

- Use comments to explain why, constraints, ownership, or non-obvious intent.
- Do not narrate code that is already clear from the implementation.
- Keep comments factual and durable.
- Delete comments that become stale.

### Documentation Comments

- All public modules must have `//!` module documentation.
- All public types, procedures, methods, transitions, and exported constants must have `///` documentation.
- Public documentation must cover purpose, important preconditions, important postconditions, ownership or capability expectations, and notable failure modes.

## Review Expectations

- Code should be understandable without relying on hidden context.
- Reviewers should be able to see authority boundaries, state transitions, and safety constraints directly in the code.
- Prefer code that is easy to verify over code that is merely short.
- If a design relies on a rule that the language can express, the rule belongs in the code.
