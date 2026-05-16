# Ultraviolet Authoring Workflow

## Default Loop

1. Identify the language area: project, module, type, expression, statement, memory, permission, authority, key, async, compile-time, FFI, diagnostic, or lowering.
2. Open `chapter-map.md` and load the smallest matching chapter file or cross-cutting guide.
3. Use `idioms-common-fixes-and-examples.md` for ordinary snippets and project shape.
4. Write source that follows the local `AGENTS.md` style: explicit visibility, explicit non-unit returns, narrow capabilities, PascalCase types/modules/files, camelCase procedures, snake_case locals/params/fields.
5. Validate with the local compiler or test surface when the task asks for runnable code or a repair.
6. Open `SPECIFICATION.md` only through a chapter file's `Spec Fallback` section for uncovered edge cases, proof requests, or source-valid/compiler-invalid conflicts.

## Source Authority

`SPECIFICATION.md` is normative. `HelloUltraviolet/Source/Reference/**` is the accepted source corpus. `README.md` explains public intent and project shape. `AGENTS.md` defines local style and the rule for compiler conformance work.

When current compiler behavior rejects, misparses, mischecks, mislowers, or miscompiles spec-valid source, preserve the source form and diagnose the canonical compiler owner path.

## Writing Defaults

- Use `Ultraviolet.toml` and a source root such as `Source`.
- Use `Main.uv` for executable roots.
- Use `public procedure main(context: Context) -> i32` or `public procedure main(move context: Context) -> i32`.
- Pass capabilities narrowly; do not thread broad context through ordinary helpers.
- Use modal types for lifecycle state, contracts/invariants for machine-checkable rules, and key blocks for shared access.
- Keep `unsafe` and `[[dynamic]]` at explicit boundaries.
