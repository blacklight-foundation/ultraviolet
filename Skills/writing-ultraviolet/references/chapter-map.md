# Chapter Map

Use this file first when routing a UV task. Every major spec chapter has a direct reference file. Guides compose multiple chapters for common authoring questions.

## Direct Chapter Routing

| Task area | Read |
| --- | --- |
| language contract, One Correct Way, source authority | `chapter-00-front-matter.md` |
| conformance, behavior classes, compile-time ordering | `chapter-01-conformance-and-notation.md` |
| diagnostics, spans, rendering, ordering | `chapter-02-diagnostics.md` |
| `Ultraviolet.toml`, assemblies, roots, artifacts | `chapter-03-project-and-compilation.md` |
| identifiers, literals, Unicode, logical lines | `chapter-04-source-text-and-lexical-structure.md` |
| parser, AST form ownership, recovery | `chapter-05-parsing-and-ast.md` |
| memory runtime, responsibility, authority, regions, provenance | `chapter-06-abstract-machine-responsibility-authority-memory.md` |
| name resolution, visibility, imports, qualified paths | `chapter-07-name-resolution-and-visibility.md` |
| type equivalence, subtyping, inference, refinements | `chapter-08-type-system.md` |
| attributes such as `[[test]]`, layout, diagnostics, FFI, memory ordering | `chapter-09-attributes.md` |
| `const`, `shared`, `unique`, receiver shorthand, admissibility | `chapter-10-permissions-and-binding-state.md` |
| imports, usings, statics, extern shell, module aggregation | `chapter-11-module-level-forms.md` |
| primitives, tuples, arrays, slices, ranges, records, enums, unions, aliases | `chapter-12-concrete-data-types.md` |
| modals, strings, bytes, pointers, functions, closures | `chapter-13-modal-and-special-types.md` |
| generics, classes, impls, associated types, dynamic objects, predicates | `chapter-14-abstraction-and-polymorphism.md` |
| procedures, receivers, contracts, invariants, verification, entrypoint | `chapter-15-procedures-and-contracts.md` |
| literals, names, access, calls, operators, construction, closures | `chapter-16-expressions.md` |
| patterns, cases, enum/modal matching, exhaustiveness | `chapter-17-patterns.md` |
| blocks, bindings, assignment, defer, region, frame, control transfer, unsafe | `chapter-18-statements-and-blocks.md` |
| key paths, acquisition, conflicts, release, speculation, memory ordering | `chapter-19-key-system.md` |
| parallel blocks, domains, spawn, dispatch, capture, cancellation | `chapter-20-structured-parallelism.md` |
| async type, suspension, composition, state machine, async-key integration | `chapter-21-async.md` |
| comptime, capabilities, reflection, quote, splice, derive | `chapter-22-compile-time-execution.md` |
| `FfiSafe`, externs, exports, attributes, foreign contracts, unwinding | `chapter-23-ffi.md` |
| cleanup, drop, runtime interface, backend, ABI, validity | `chapter-24-lowering-lifecycle-and-backend.md` |

## Guide Routing

| Prompt keywords | Read |
| --- | --- |
| move, ownership, responsibility, partial move, drop, lifetime | `guide-memory-responsibility-permissions.md` |
| context, capability, authority, filesystem, network, effect | `guide-authority-capabilities-and-effects.md` |
| region, frame, provenance, allocation, `^` | `guide-regions-frames-and-provenance.md` |
| shared, key, fence, ordering, speculative, release | `guide-keys-shared-access-and-memory-ordering.md` |
| procedure, receiver, contract, invariant, `[[test]]`, verification | `guide-procedures-contracts-tests-and-verification.md` |
| modal, state, transition, resource lifecycle | `guide-modal-resource-lifecycle.md` |
| parallel, dispatch, spawn, async, yield, sync, race, cancellation | `guide-async-parallelism-and-cancellation.md` |
| unsafe, extern, FFI, ABI, runtime, lowering | `guide-unsafe-ffi-runtime-and-lowering.md` |
| common syntax, snippets, project templates | `idioms-common-fixes-and-examples.md` |
| compiler disagrees with spec-valid source | `compiler-mismatch-workflow.md` |

## Reference Corpus Map

Spec chapters map to `HelloUltraviolet/Source/Reference` folders: `Projects`, `SourceText`, `Parsing`, `Conformance`, `Diagnostics`, `Authority`, `Names`, `Types`, `Attributes`, `Permissions`, `Modules`, `DataTypes`, `ModalTypes`, `Polymorphism`, `Procedures`, `Expressions`, `Patterns`, `Statements`, `Keys`, `Parallelism`, `Async`, `Comptime`, `FFI`, and `Lowering`.
