## Behavior and Expectations

- You will NOT continuously provide negative framing. When explaining or planning, avoid negative framing by default. Do not repeatedly say what a concept is not, what alternatives are rejected, or what should not be done unless the distinction is necessary to prevent a likely mistake. Prefer direct affirmative descriptions of the intended design, behavior, and next action. If a constraint matters, state it once in the relevant decision record or plan section, then proceed using the approved design without re-litigating rejected options.
- Do NOT use "stage#" or "phase#" as a naming convention; ever.
- Do NOT use vanity prefixes or suffixes unless there is an explicit need to prevent potential naming collisions, such as public facing API/ABIs.
- Spec-valid source is authoritative evidence. When the current compiler rejects, misparses, mischecks, mislowers, or miscompiles source that conforms to the authoritative specification, repair the canonical compiler implementation: parser, resolver, typechecker, lowering, runtime, diagnostics, or whichever path owns the defect. Preserve the source form unless the source itself violates the specification, has an independent code-quality defect, or the user explicitly requests a source rewrite.

These rules apply to every task in this project unless explicitly overridden.
Bias: caution over speed on non-trivial work. Use judgment on trivial tasks.

## Rule 1 — Think Before Coding
State assumptions explicitly. If uncertain, ask rather than guess.
Present multiple interpretations when ambiguity exists.
Push back when a simpler approach exists.
Stop when confused. Name what's unclear.

## Rule 2 — Simplicity First
Minimum code that solves the problem. Nothing speculative.
No features beyond what was asked. No abstractions for single-use code.
Test: would a senior engineer say this is overcomplicated? If yes, simplify.

## Rule 3 — Surgical Changes
Touch only what you must. Clean up only your own mess.
Don't "improve" adjacent code, comments, or formatting.
Don't refactor what isn't broken. Match existing style.

## Rule 4 — Goal-Driven Execution
Define success criteria. Loop until verified.
Don't follow steps. Define success and iterate.
Strong success criteria let you loop independently.

## Rule 5 — Use the model only for judgment calls
Use me for: classification, drafting, summarization, extraction.
Do NOT use me for: routing, retries, deterministic transforms.
If code can answer, code answers.

## Rule 6 — Token budgets are not advisory
Per-task: 4,000 tokens. Per-session: 30,000 tokens.
If approaching budget, summarize and start fresh.
Surface the breach. Do not silently overrun.

## Rule 7 — Surface conflicts, don't average them
If two patterns contradict, pick one (more recent / more tested).
Explain why. Flag the other for cleanup.
Don't blend conflicting patterns.

## Rule 8 — Read before you write
Before adding code, read exports, immediate callers, shared utilities.
"Looks orthogonal" is dangerous. If unsure why code is structured a way, ask.

## Rule 9 — Tests verify intent, not just behavior
Tests must encode WHY behavior matters, not just WHAT it does.
A test that can't fail when business logic changes is wrong.

## Rule 10 — Checkpoint after every significant step
Summarize what was done, what's verified, what's left.
Don't continue from a state you can't describe back.
If you lose track, stop and restate.

## Rule 11 — Match the codebase's conventions, even if you disagree
Conformance > taste inside the codebase.
If you genuinely think a convention is harmful, surface it. Don't fork silently.

## Rule 12 — Fail loud
"Completed" is wrong if anything was skipped silently.
"Tests pass" is wrong if any were skipped.
Default to surfacing uncertainty, not hiding it.

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
