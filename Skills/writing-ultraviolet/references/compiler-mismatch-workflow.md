# Compiler Mismatch Workflow

## Load When

Use when the current compiler rejects, misparses, mischecks, mislowers, miscompiles, or gives misleading diagnostics for source that appears valid under the spec.

## Workflow

1. Identify the source form and open the owning chapter reference.
2. Check the local reference corpus for an accepted specimen of the same form.
3. If the source matches the chapter rules, preserve the source form.
4. Reproduce the compiler behavior with the exact `uv.exe` path used.
5. Classify the likely owner: parser, resolver, typechecker, permission/provenance checker, contract verifier, lowering, runtime, backend, diagnostics, or project loader.
6. Repair the canonical implementation when asked to fix code. Do not rewrite spec-valid source to match an implementation defect.

## Evidence To Capture

- Source path and relevant snippet.
- Spec chapter and section.
- Reference specimen path.
- Compiler executable path.
- Command and output.
- Owner path and failure mode.

## Common Owner Routing

- Parse or AST shape failure: chapters 4, 5, Appendix B, Appendix C.
- Name or visibility failure: chapters 7 and 11.
- Type, permission, memory, or provenance failure: chapters 8, 10, 6, 18.
- Contract or entrypoint failure: chapter 15.
- Key/shared failure: chapter 19.
- Async/parallel failure: chapters 20 and 21.
- FFI/runtime/lowering failure: chapters 23 and 24.
- Diagnostic shape failure: chapter 2 and Appendix A.
