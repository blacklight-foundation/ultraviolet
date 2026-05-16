# Skill Evals

## Trigger Evals

These prompts should trigger the skill:

- Write a minimal Ultraviolet executable.
- Review this `.uv` source for permission and move errors.
- Explain why `context.fs~>write_stdout` uses `.`, `~>`, and not `::`.
- Write a `[[test]]` procedure with a postcondition.
- Diagnose why the compiler rejects this modal transition.
- Write a shared counter update with memory ordering.
- Build a safe wrapper around an extern FFI call.

## Non-Trigger Evals

These prompts should not trigger this skill unless they mention UV:

- Write Python code to parse JSON.
- Explain Rust ownership.
- Create a generic Markdown checklist.

## Output Assertions

- Reads `SKILL.md`, `chapter-map.md`, then focused chapter or guide files.
- Does not open bundled `references/SPECIFICATION.md` for routine authoring.
- Uses bundled `references/SPECIFICATION.md` for spec fallback instead of the repository-root spec path.
- Uses `.uv`, `Ultraviolet.toml`, and `Source/Main.uv` for minimal project examples.
- Uses explicit visibility, explicit return types, and explicit non-unit `return`.
- Uses `.` for field access, `::` for qualification, and `~>` for method calls.
- Uses `#` key blocks for shared access.
- Preserves spec-valid source when diagnosing compiler mismatch.

## Scenario Evals

- Generate a project with `main(context: Context) -> i32`.
- Generate record, enum, tuple, array, slice, range, union, and alias examples.
- Generate generic class and implementation examples.
- Generate procedure contracts and invariants.
- Generate expression, pattern, and statement examples.
- Generate partial move and cleanup examples.
- Generate key ordering and fence examples.
- Generate modal lifecycle examples.
- Generate async and parallel examples.
- Generate FFI wrapper examples.
