# Comprehensive Rewrite Plan: `writing-ultraviolet` as a Complete UV Authoring Package

## Summary

Rewrite `Skills/writing-ultraviolet` so it functions as a complete LLM authoring
package for Ultraviolet, not a chapter index. The package must let Codex or another
LLM design, write, review, repair, and explain idiomatic UV source across the full
language surface while using the bundled `references/SPECIFICATION.md` only for
proof, ambiguity, or exact normative fallback.

The source of truth remains `C:\Dev\Ultraviolet\Skills\writing-ultraviolet`. The
installed target remains
`\\wsl.localhost\Ubuntu\home\crow\.codex\skills\writing-ultraviolet`. The bundled
spec must be byte-synchronized from repository-root `SPECIFICATION.md` before
installation.

Success criteria:

- Broad UV generation prompts first load whole-language authoring doctrine, then
  the smallest relevant construct guides.
- Every major construct in spec chapters 0-24 and appendices A-D has authoring
  guidance, syntax examples, static semantics, runtime/lowering notes, diagnostics,
  and fallback anchors.
- Examples are spec-backed or corpus-backed, with source evidence paths in the guide
  text.
- Guardrails catch fake APIs, invalid escapes, invented syntax, missing coverage,
  stale spec copies, and weak routing.
- The WSL installed copy is synchronized and validated after the repo copy passes.

## Current State To Replace

The current package has useful raw materials: `SKILL.md`, a bundled spec, chapter
summaries, a few thin guides, helper scripts, `agents/openai.yaml`, a minimal project
asset, and a 156-file `HelloUltraviolet/Source/Reference/**` corpus covering Async,
Attributes, Authority, Comptime, Conformance, DataTypes, Diagnostics, Expressions,
FFI, Keys, Lowering, ModalTypes, Modules, Names, Parallelism, Parsing, Patterns,
Permissions, Polymorphism, Procedures, Projects, SourceText, Statements, and Types.

The rewrite must preserve the useful assets but change the skill's shape:

- `SKILL.md` becomes a concise loading and workflow router.
- `references/guide-idiomatic-ultraviolet-authoring.md` becomes the primary authoring
  doctrine.
- Topical guides become complete construct manuals.
- Chapter files become compact spec-indexed authoring references.
- Scripts become enforcement tools, not just convenience utilities.
- `evals.md` becomes an acceptance suite for the skill itself.

## Package Architecture

Keep this top-level structure:

```text
Skills/writing-ultraviolet/
  SKILL.md
  agents/openai.yaml
  assets/minimal-project/
  references/
  scripts/
```

Create or replace these primary guides:

```text
references/guide-idiomatic-ultraviolet-authoring.md
references/guide-projects-modules-and-source-text.md
references/guide-data-modeling-and-type-design.md
references/guide-refinements-predicates-and-polymorphism.md
references/guide-attributes-and-owned-features.md
references/guide-permissions-responsibility-and-binding-state.md
references/guide-authority-capabilities-and-effects.md
references/guide-regions-frames-and-provenance.md
references/guide-contracts-invariants-tests-and-verification.md
references/guide-modal-resource-lifecycle.md
references/guide-expressions-patterns-statements-and-control-flow.md
references/guide-keys-shared-access-and-memory-ordering.md
references/guide-async-parallelism-and-execution-domains.md
references/guide-compile-time-metaprogramming.md
references/guide-unsafe-ffi-runtime-and-lowering.md
references/guide-diagnostics-conformance-and-compiler-mismatches.md
references/idioms-common-fixes-and-examples.md
references/language-surface-checklist.md
references/chapter-map.md
references/evals.md
```

Retain and rewrite all chapter and appendix references:

```text
references/chapter-00-front-matter.md
references/chapter-01-conformance-and-notation.md
references/chapter-02-diagnostics.md
references/chapter-03-project-and-compilation.md
references/chapter-04-source-text-and-lexical-structure.md
references/chapter-05-parsing-and-ast.md
references/chapter-06-abstract-machine-responsibility-authority-memory.md
references/chapter-07-name-resolution-and-visibility.md
references/chapter-08-type-system.md
references/chapter-09-attributes.md
references/chapter-10-permissions-and-binding-state.md
references/chapter-11-module-level-forms.md
references/chapter-12-concrete-data-types.md
references/chapter-13-modal-and-special-types.md
references/chapter-14-abstraction-and-polymorphism.md
references/chapter-15-procedures-and-contracts.md
references/chapter-16-expressions.md
references/chapter-17-patterns.md
references/chapter-18-statements-and-blocks.md
references/chapter-19-key-system.md
references/chapter-20-structured-parallelism.md
references/chapter-21-async.md
references/chapter-22-compile-time-execution.md
references/chapter-23-ffi.md
references/chapter-24-lowering-lifecycle-and-backend.md
references/appendix-a-diagnostic-index.md
references/appendix-b-complete-grammar.md
references/appendix-c-ast-form-index.md
references/appendix-d-layout-abi-runtime.md
references/SPECIFICATION.md
```

Remove obsolete or superseded guide names from routing after replacement. In
particular, replace `guide-memory-responsibility-permissions.md`,
`guide-procedures-contracts-tests-and-verification.md`, and
`guide-async-parallelism-and-cancellation.md` with the more complete names above,
then update all references and validators.

## `SKILL.md` Rewrite

`SKILL.md` must stay short and procedural. It should contain:

- Frontmatter with only `name` and `description`.
- A description broad enough to trigger for writing, reviewing, debugging, repairing,
  and explaining UV source, manifests, tests, contracts, modals, refinements,
  attributes, permissions, ownership, regions, keys, async, parallelism, GPU/CPU
  domains, compile-time code, FFI, diagnostics, lowering, and runtime behavior.
- A required workflow:
  1. For any broad authoring task, read `guide-idiomatic-ultraviolet-authoring.md`.
  2. Route specific constructs through `find_uv_topic.py` or `chapter-map.md`.
  3. Load one topical guide plus relevant chapter references.
  4. Use `language-surface-checklist.md` for multi-construct or unfamiliar tasks.
  5. Use bundled `SPECIFICATION.md` for exact proof, ambiguity, compiler/spec
     conflicts, or user-requested citations.
  6. Use the compiler-mismatch guide when the compiler rejects spec-valid source.
- A resource index grouped by purpose:
  - Start here authoring doctrine.
  - Whole-language routing.
  - Construct guides.
  - Chapter references.
  - Assets and scripts.

`agents/openai.yaml` must be updated to reflect the new role: a complete Ultraviolet
authoring package, not just "author correct code."

## Whole-Language Authoring Doctrine

Create `guide-idiomatic-ultraviolet-authoring.md` as the first-read guide for broad
source generation. It must contain compact decision tables, not prose-only advice.

Required sections:

- **Authoring Priority**: express correctness in types, modal state, contracts,
  invariants, refinements, capability boundaries, permissions, keys, and regions
  before ordinary runtime checks.
- **Design Surface Matrix**:
  - `record`: value data, snapshots, config, results.
  - `class`: behavior contract and polymorphic interface.
  - `enum`: closed named alternatives.
  - `union`: typed success/failure or alternative payloads.
  - `modal`: lifecycle or state-dependent fields/operations.
  - type alias: domain name or signature simplification.
  - refinement: value subset with predicate-backed meaning.
  - opaque type: hide implementation while exposing promised behavior.
  - dynamic class object: runtime dispatch when intended.
  - capability class: authority-bearing behavior abstraction.
- **Constraint Placement Matrix**:
  - refinement for reusable value-domain constraints;
  - precondition for caller obligation;
  - postcondition for return guarantee;
  - invariant for type/state preservation;
  - modal state for operation availability;
  - predicate/class bound for generic semantic requirements;
  - `[[test]]` postcondition for source-native tests;
  - dynamic check only when static expression is unavailable or runtime data is
    required.
- **Authority and Effects**:
  - `Context` fields: `io`, `net`, `heap`, `reactor`, `sys`, `time`;
  - execution-domain methods: `context~>inline()`, `context~>cpu()`,
    `context~>gpu()`;
  - pass the narrowest capability actually used;
  - model external effects as explicit capability calls.
- **Memory and Access**:
  - separate responsibility, permission, mutability, and binding activity;
  - use `move` for responsibility transfer;
  - use `const`, `shared`, and `unique` as access contracts;
  - use keys for shared access;
  - use regions/frames for scoped allocation and provenance.
- **State and Lifecycle**:
  - use modal types for lifecycle protocols;
  - keep state-specific fields and methods inside states;
  - use transitions for state changes;
  - use modal patterns for branching by state.
- **Concurrency and Suspension**:
  - choose `parallel`/`spawn` for scoped concurrent work;
  - choose `dispatch` for partitioned indexed work;
  - choose `Async`/`Stream` when suspension is semantic;
  - use `sync`, `all`, `race`, and combinators for structured async composition;
  - preserve key and permission rules across capture and suspension.
- **Boundaries**:
  - isolate unsafe, raw pointers, externs, hosted exports, and ABI-facing code;
  - wrap boundaries in safe APIs that restore ownership, lifetime, capability,
    layout, and unwind guarantees.
- **Review Checklist**:
  - source visibly expresses authority, effects, ownership, permissions, state,
    allocation, synchronization, suspension, and unsafe boundaries;
  - examples use valid literals, valid module/type qualification, valid receiver
    syntax, valid transition syntax, and corpus-backed host capabilities.

## Construct Guide Requirements

Each topical guide must use this structure:

```markdown
# Guide: <Topic>

## Load When
## Authoring Decisions
## Canonical Forms
## Design Patterns
## Common Failures
## Spec Anchors
## Reference Corpus
```

Each guide must include exact spec anchors by chapter/section and corpus paths. UV
code fences must be small, valid, and source-backed.

Required guide content:

- `guide-projects-modules-and-source-text.md`:
  - manifests, assemblies, roots, module directories, source roots, file aggregation,
    import/using forms, module-level `let`/`var`, extern shells, visibility, doc
    comments, identifiers, literals, comments, Unicode normalization, string/char
    escape rules, and minimal project shape.
- `guide-data-modeling-and-type-design.md`:
  - primitives, tuples, arrays, slices, ranges, records, enums, unions, aliases,
    construction, patterns, layout implications, and choosing the right data form.
- `guide-refinements-predicates-and-polymorphism.md`:
  - type equivalence/subtyping/inference, refinements, generic parameter semicolons,
    generic argument commas, predicates, classes, implementations, associated types,
    dynamic class objects, opaque types, capability classes, and foundational
    predicates.
- `guide-attributes-and-owned-features.md`:
  - owner matrix for every attribute family: test, diagnostic, vendor, layout,
    optimization, dynamic/static, reflection, emission, derive, memory ordering,
    export, hosted export, mangle, unwind, foreign contract.
- `guide-permissions-responsibility-and-binding-state.md`:
  - `const`/`shared`/`unique`, receiver shorthand `~`/`~%`/`~!`, admissibility,
    alias/exclusivity, active unique suspension, whole moves, field moves, partial
    moves, cleanup responsibility, `let`/`var`, shared access through keys, and
    permission-aware API design.
- `guide-authority-capabilities-and-effects.md`:
  - no ambient authority, `Context` capability roots, capability attenuation, `~>`
    capability calls, host IO through `context.io`/`$IO`, network/system/time/heap/
    reactor capabilities, execution-domain methods, and effect visibility.
- `guide-regions-frames-and-provenance.md`:
  - `region`, `frame`, `^`, named-region allocation, active region targets, frame
    reset behavior, provenance categories, escape prevention, stack/heap/region
    choice, and unsafe manual region APIs as boundary-only material.
- `guide-contracts-invariants-tests-and-verification.md`:
  - procedure declarations, methods, receivers, overloads, contract grammar,
    preconditions, postconditions, `@entry`, `@result`, invariants, verification-pure
    expressions, behavioral subtyping, executable entry rules, and `[[test]]`
    postcondition requirements.
- `guide-modal-resource-lifecycle.md`:
  - modal declarations, states, state fields, state methods, transitions,
    `Type@State`, state literals, widening, modal patterns, lifecycle API shape,
    cancellation/resource tokens, safe pointers, managed strings/bytes, and cleanup/
    lowering effects.
- `guide-expressions-patterns-statements-and-control-flow.md`:
  - literal/name/access/call/operator/cast/transmute/construction expressions, `if`,
    `if is`, `case`, `loop`, iterator loops, propagation `?`, ranges, closures,
    pipelines, blocks, bindings, local using, assignment, expression statements,
    `defer`, region/frame statements, `return`, `break`, `continue`, unsafe
    statements, and pattern exhaustiveness.
- `guide-keys-shared-access-and-memory-ordering.md`:
  - key paths, field/index/coarsened paths, read/write acquisition, ordered multi-key
    blocks, nested release, conflict detection, speculative execution, dynamic
    verification, fences, memory ordering attributes, parallel dispatch keys, and
    async-key suspension.
- `guide-async-parallelism-and-execution-domains.md`:
  - `parallel`, inline/CPU/GPU domains, execution-domain capability selection,
    capture rules, `spawn`, `wait`, `dispatch`, key modes, reductions, ordered
    dispatch, cancellation, panic propagation, determinism, nesting, `Async`,
    `Stream`, `yield`, `yield release`, `yield from`, `sync`, `resume`, async
    iteration, `all`, `race`, `map`, `filter`, `take`, `fold`, `chain`, `until`,
    and async state-machine implications.
- `guide-compile-time-metaprogramming.md`:
  - `comptime` blocks, `comptime if`, `comptime loop`, `comptime procedure`,
    compile-time capabilities, reflection, `type_name`, quote/splice/emission,
    derive targets, generated declarations, compile-time diagnostics, and runtime-
    authority visibility.
- `guide-unsafe-ffi-runtime-and-lowering.md`:
  - `unsafe`, raw pointers, `FfiSafe`, extern blocks, exported procedures, hosted
    exports, FFI attributes, capability isolation, foreign contracts, boundary
    unwinding, layout/ABI, symbols/mangling/linkage, initialization, cleanup/drop/
    unwinding, runtime interface, backend validity, and source patterns that lower
    predictably.
- `guide-diagnostics-conformance-and-compiler-mismatches.md`:
  - diagnostic records/spans/rendering, behavior classes, normative vs implementation
    behavior, owner routing, compiler mismatch evidence, preserving spec-valid
    source, and repair routing to parser/resolver/typechecker/permission/provenance/
    lowering/runtime/backend/diagnostics.

## Chapter Reference Rewrite Requirements

Every `chapter-XX-*.md` file must be rewritten or audited to match this required
template:

```markdown
# Chapter N: <Title>

## Load When
## Spec Sections
## Authoring Rules
## Syntax Forms
## Static Semantics
## Runtime and Lowering Notes
## Diagnostics
## Reference Corpus
## Spec Fallback
```

Rules for chapter files:

- `Spec Sections` lists every section in the corresponding spec chapter.
- `Authoring Rules` states practical writing rules, not just chapter scope.
- `Syntax Forms` includes only validated, source-backed examples.
- `Static Semantics` names type/resolution/permission/verification checks.
- `Runtime and Lowering Notes` names observable behavior, cleanup, ABI, dispatch,
  key, async, or backend implications.
- `Diagnostics` lists failure categories and routes detailed code lookup to Appendix A.
- `Reference Corpus` lists exact `HelloUltraviolet/Source/Reference/...` folders or
  files.
- `Spec Fallback` gives exact chapter/section fallback text.

Chapter content must cover:

- 0-1: language design contract, conformance, behavior kinds, compile-time ordering,
  target/ABI assumptions.
- 2-5: diagnostics, manifests/project loading, source text/literals, parser/AST.
- 6-10: authority, host primitives, memory, permissions, resolution, types, attributes.
- 11-15: modules, concrete data, modal/special types, polymorphism/refinements,
  procedures/contracts.
- 16-18: expressions, patterns, statements/blocks/regions/frames/unsafe.
- 19-22: keys, parallelism/domains/GPU, async, compile-time metaprogramming.
- 23-24: FFI, unsafe boundaries, lowering, runtime, ABI, backend.
- Appendices: diagnostic index, grammar, AST forms, layout/ABI/runtime references.

## Routing And Discoverability

Rewrite `chapter-map.md` into three tables:

- Direct chapter routing: every chapter and appendix with major keywords.
- Topical guide routing: all new guide files and their trigger keywords.
- Corpus routing: every `HelloUltraviolet/Source/Reference/*` folder mapped to guides
  and chapter files.

Rewrite `language-surface-checklist.md` into an acceptance checklist with one row per
language system:

- project/model;
- source text/literals;
- modules/names/visibility;
- data types;
- type system/refinement;
- attributes;
- permissions/binding state;
- authority/capabilities/effects;
- regions/frames/provenance;
- procedures/contracts/tests;
- modal/special types;
- polymorphism/classes/opaque/dynamic/capability classes;
- expressions/control/propagation;
- patterns;
- statements/blocks/defer/unsafe;
- keys/memory ordering;
- parallelism/execution domains/GPU;
- async/streams/composition;
- compile-time/reflection/emission/derive;
- FFI/unsafe/raw pointers;
- lowering/runtime/ABI/backend;
- diagnostics/compiler mismatch.

Update `find_uv_topic.py`:

- Keep default interface: `python find_uv_topic.py <query...>`.
- Add `--all` to print all positive matches in score order.
- Add `--json` to print machine-readable matches.
- Always include `guide-idiomatic-ultraviolet-authoring.md` for broad authoring,
  design, generation, review, repair, or multi-surface prompts.
- Route GPU/domain queries to `guide-async-parallelism-and-execution-domains.md`,
  `chapter-20-structured-parallelism.md`, and
  `guide-authority-capabilities-and-effects.md`.
- Route refinement/predicate/opaque/dynamic/class/capability-class queries to
  `guide-refinements-predicates-and-polymorphism.md`, chapter 8, and chapter 14.
- Route attribute queries to `guide-attributes-and-owned-features.md`, chapter 9, and
  owner chapters such as 20, 22, or 23 when keywords match.
- Route compiler diagnostics or implementation mismatch queries to the diagnostics/
  conformance guide plus Appendix A and the owner chapter.

## Evidence And Example Policy

Every UV example added or retained must satisfy one of these evidence categories:

- copied directly from `HelloUltraviolet/Source/Reference/**`;
- copied directly from `SPECIFICATION.md`;
- copied from `assets/minimal-project`;
- synthesized only from adjacent spec/corpus forms, with a Markdown note naming the
  source forms used.

Each executable-looking example must have a nearby line like:

```markdown
Source evidence: `HelloUltraviolet/Source/Reference/Parallelism/ExecutionDomains.uv`.
```

Invalid examples are allowed only in diagnostic sections and must be explicitly
labeled as invalid. Diagnostic examples must never be presented as source patterns.

Do not keep examples that use fictional APIs, schematic module paths, invented
capability names, invalid transition syntax, invalid string escapes, or placeholder
modules.

## Script Changes

Update `validate_skill.py` to enforce:

- required file inventory includes all new guide names and no superseded guide names;
- `SKILL.md` frontmatter has only `name` and `description`;
- `SKILL.md` references the idiomatic authoring guide and all routing files;
- every chapter and appendix reference has the required headings;
- every chapter file lists spec sections for its chapter;
- every topical guide has Load When, Authoring Decisions, Canonical Forms, Design
  Patterns, Common Failures, Spec Anchors, and Reference Corpus;
- every referenced guide/chapter file is discoverable from `SKILL.md`,
  `chapter-map.md`, or `find_uv_topic.py`;
- bundled `references/SPECIFICATION.md` is byte-identical to repository-root
  `SPECIFICATION.md` when `--repo-root` is provided;
- all Markdown files outside the bundled spec are ASCII;
- UV fences have balanced delimiters, valid string/char escapes, and no known-bad
  syntax patterns;
- stale standalone escape guide does not exist;
- forbidden guidance patterns are absent outside validator tests: `context.fs`,
  `$FileSystem`, `FileSystem::`, raw JSON `"\b"`/`"\f"`, `\x08`, `\x0C`, `\x5C`,
  `\x22`, fictional `Result::`, `context.execution_domain`, invalid transition return
  forms, and non-corpus module paths;
- each guide has at least one spec anchor and one corpus/spec evidence path;
- each corpus folder maps to at least one guide and one chapter reference.

Update `collect_uv_examples.py`:

- Keep no-argument coverage summary.
- Add `--topic <name-or-keyword>` to list matching corpus files.
- Add `--max <n>` to limit example listing.
- Add `--missing-only` to report missing corpus mappings.
- Add `--json` for machine-readable coverage.

Update `find_uv_topic.py` as described above and add script-level tests through direct
command invocations in the validation plan.

## Acceptance Evals

Rewrite `references/evals.md` into a skill acceptance suite. Each eval must include:
prompt, files expected to load, output assertions, and failure patterns.

Required evals:

- Minimal project: generate manifest plus `main(context: Context) -> i32` using
  `context.io~>write_stdout`.
- String/JSON escaping: emit valid UV literals for backslash, quote, `\\b`, `\\f`,
  decoded backspace char, and decoded form-feed char.
- Modules/imports: create module-directory-aware code with explicit visibility and
  valid `using`.
- Data modeling: choose record/enum/union/modal/refinement correctly for a domain
  problem.
- Refinements/predicates: use refinement or predicate where the rule is reusable and
  statically expressible.
- Attributes: apply layout/test/reflect/export attributes only to owned forms.
- Permissions: choose `const`, `shared`, `unique`, receiver shorthand, and explicit
  `move`.
- Regions: use `region`, `frame`, `^`, and named region allocation without provenance
  escape.
- Contracts/tests: add preconditions, postconditions, invariants, and a valid
  `[[test]]`.
- Modal lifecycle: model a resource with states, state fields, transitions, widening,
  and modal patterns.
- Keys/shared access: use read/write key blocks and release where appropriate.
- Parallel/GPU: select execution domain, use `parallel`, `dispatch`, key mode,
  ordered/reduce options, and capture rules.
- Async: use `Async`, `Stream`, `yield release`, `sync`, `all`, `race`, combinators,
  and async-key rules.
- Compile-time: use reflection, quote/splice/emission, derive, and compile-time
  diagnostics without hiding runtime authority.
- FFI/unsafe: isolate extern/raw pointer/export/hosted export code behind safe wrappers
  with ownership/lifetime/capability notes.
- Compiler mismatch: preserve spec-valid source and route the suspected defect to the
  owning compiler subsystem.

## Implementation Order

1. Refresh bundled `references/SPECIFICATION.md` from root `SPECIFICATION.md`.
2. Rewrite `SKILL.md` and `agents/openai.yaml` to describe the new package role and
   loading workflow.
3. Add the central idiomatic authoring guide.
4. Add or replace all topical construct guides.
5. Rewrite `chapter-map.md` and `language-surface-checklist.md` for full language
   routing.
6. Audit and rewrite all chapter and appendix files to the required template.
7. Update helper scripts and validators.
8. Rewrite `evals.md`.
9. Run validation on the repo skill.
10. Fix all validation failures.
11. Sync the validated package to the WSL Codex skill directory.
12. Run the same validations against the installed WSL copy.

## Validation Commands

Run these from `C:\Dev\Ultraviolet`:

```powershell
python Skills\writing-ultraviolet\scripts\validate_skill.py Skills\writing-ultraviolet --repo-root C:\Dev\Ultraviolet
python -m py_compile Skills\writing-ultraviolet\scripts\validate_skill.py Skills\writing-ultraviolet\scripts\find_uv_topic.py Skills\writing-ultraviolet\scripts\collect_uv_examples.py
python Skills\writing-ultraviolet\scripts\collect_uv_examples.py --missing-only
```

Run routing checks:

```powershell
python Skills\writing-ultraviolet\scripts\find_uv_topic.py modal lifecycle transition contracts
python Skills\writing-ultraviolet\scripts\find_uv_topic.py type refinement predicate class opaque capability
python Skills\writing-ultraviolet\scripts\find_uv_topic.py attributes layout dynamic reflect derive export unwind
python Skills\writing-ultraviolet\scripts\find_uv_topic.py permissions unique shared const partial move receiver
python Skills\writing-ultraviolet\scripts\find_uv_topic.py region frame provenance allocation
python Skills\writing-ultraviolet\scripts\find_uv_topic.py shared key memory ordering release speculative
python Skills\writing-ultraviolet\scripts\find_uv_topic.py async stream sync race all resume yield release
python Skills\writing-ultraviolet\scripts\find_uv_topic.py parallel dispatch gpu execution domain reduce ordered
python Skills\writing-ultraviolet\scripts\find_uv_topic.py comptime reflection quote splice emit derive
python Skills\writing-ultraviolet\scripts\find_uv_topic.py ffi hosted export raw pointer foreign contract unwind
python Skills\writing-ultraviolet\scripts\find_uv_topic.py string json escape backslash quote form feed backspace
```

Run guardrail searches:

```powershell
rg -n -F -e 'context.fs' -e '$FileSystem' -e 'FileSystem::' Skills\writing-ultraviolet --glob '!SPECIFICATION.md'
rg -n -F -e '\x08' -e '\x0C' -e '\x5C' -e '\x22' Skills\writing-ultraviolet --glob '!SPECIFICATION.md'
rg -n -F -e '"\b"' -e '"\f"' Skills\writing-ultraviolet --glob '!SPECIFICATION.md'
rg -n -i 'stage#|phase#' Skills\writing-ultraviolet
```

Run compiler checks where available:

```powershell
..\Build\bin\uv.exe check
```

If the local compiler still reports `E-CLI-0002 compiler pipeline unavailable`, record
that exact result and do not claim compiler validation. The package can still pass
spec/corpus-backed validation.

After syncing to WSL, run the same validation commands against:

```text
\\wsl.localhost\Ubuntu\home\crow\.codex\skills\writing-ultraviolet
```

## Assumptions And Defaults

- The rewrite is allowed to replace existing guide files when their names or scope are
  too narrow.
- The package prioritizes authoring correctness and complete UV coverage over
  minimizing reference count.
- `references/SPECIFICATION.md` remains bundled because the skill must work outside
  the repo.
- The `HelloUltraviolet/Source/Reference/**` corpus is the preferred source of examples
  before synthesizing new ones.
- The minimal project asset remains part of the skill and must use valid `context.io`
  host IO.
- The implementation must not add README, changelog, installation docs, or other
  auxiliary files inside the skill package.
- Independent forward-testing can be added after explicit authorization; the required
  acceptance gate for this rewrite is deterministic local validation plus the eval
  prompts in `references/evals.md`.
