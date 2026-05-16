---
name: writing-ultraviolet
description: Use when writing, reviewing, explaining, debugging, or repairing Ultraviolet (.uv) source, Ultraviolet.toml manifests, tests, contracts, modal types, memory/lifetime code, responsibility, ownership, move semantics, permissions, capabilities, authority, keys, shared access, regions, frames, async, parallelism, compile-time code, FFI, unsafe boundaries, diagnostics, lowering, runtime behavior, or compiler-conformance examples.
---

# Writing Ultraviolet

## Overview

Use this skill to write and review correct, idiomatic Ultraviolet source while minimizing routine reads of `SPECIFICATION.md`. The skill is organized by the Ultraviolet specification chapters first, then by cross-cutting authoring guides.

## Workflow

1. Read `references/authoring-workflow.md` for the default authoring loop.
2. Read `references/chapter-map.md` to route the task to chapter files and guides.
3. Read the smallest relevant chapter file or guide. Prefer one focused file plus `references/idioms-common-fixes-and-examples.md` for normal code generation.
4. Use `SPECIFICATION.md` only when a reference file explicitly says to fall back, when the user asks for proof, or when source-valid behavior conflicts with compiler behavior.
5. If the compiler rejects source that matches the authoritative spec, use `references/compiler-mismatch-workflow.md` and preserve the spec-valid source form unless the source has an independent defect.

## Chapter References

- `references/chapter-00-front-matter.md`
- `references/chapter-01-conformance-and-notation.md`
- `references/chapter-02-diagnostics.md`
- `references/chapter-03-project-and-compilation.md`
- `references/chapter-04-source-text-and-lexical-structure.md`
- `references/chapter-05-parsing-and-ast.md`
- `references/chapter-06-abstract-machine-responsibility-authority-memory.md`
- `references/chapter-07-name-resolution-and-visibility.md`
- `references/chapter-08-type-system.md`
- `references/chapter-09-attributes.md`
- `references/chapter-10-permissions-and-binding-state.md`
- `references/chapter-11-module-level-forms.md`
- `references/chapter-12-concrete-data-types.md`
- `references/chapter-13-modal-and-special-types.md`
- `references/chapter-14-abstraction-and-polymorphism.md`
- `references/chapter-15-procedures-and-contracts.md`
- `references/chapter-16-expressions.md`
- `references/chapter-17-patterns.md`
- `references/chapter-18-statements-and-blocks.md`
- `references/chapter-19-key-system.md`
- `references/chapter-20-structured-parallelism.md`
- `references/chapter-21-async.md`
- `references/chapter-22-compile-time-execution.md`
- `references/chapter-23-ffi.md`
- `references/chapter-24-lowering-lifecycle-and-backend.md`

## Appendix References

- `references/appendix-a-diagnostic-index.md`
- `references/appendix-b-complete-grammar.md`
- `references/appendix-c-ast-form-index.md`
- `references/appendix-d-layout-abi-runtime.md`

## Authoring Guides

- `references/guide-memory-responsibility-permissions.md`
- `references/guide-authority-capabilities-and-effects.md`
- `references/guide-regions-frames-and-provenance.md`
- `references/guide-keys-shared-access-and-memory-ordering.md`
- `references/guide-procedures-contracts-tests-and-verification.md`
- `references/guide-modal-resource-lifecycle.md`
- `references/guide-async-parallelism-and-cancellation.md`
- `references/guide-unsafe-ffi-runtime-and-lowering.md`
- `references/idioms-common-fixes-and-examples.md`
- `references/compiler-mismatch-workflow.md`
- `references/evals.md`

## Assets and Scripts

- Use `assets/minimal-project/` when creating a new minimal project.
- Use `scripts/find_uv_topic.py` to route a topic to references.
- Use `scripts/collect_uv_examples.py` to inspect the local reference corpus.
- Use `scripts/validate_skill.py` after editing this skill.
