# Chapter 3: Project and Compilation Model

## Load When

Use for `Ultraviolet.toml`, assemblies, source roots, module discovery, output artifacts, linking, tool resolution, IR inputs, and project diagnostics.

## Authoring Rules

- A project has an `Ultraviolet.toml`; do not rely on single-file fallback.
- Use `[[assembly]]` entries with `name`, `kind`, and `root`.
- Use `Source` or another explicit root directory and keep roots non-overlapping.
- Executable roots should contain exactly one valid public `main`.

## Syntax Forms

```toml
[[assembly]]
name = "HelloUltraviolet"
kind = "executable"
root = "Source"

[build]
incremental = true
progress = true
```

## Static Semantics

Assembly names are identifiers, roots are relative project paths, library `link_kind` may be `shared` or `static`, and unknown top-level or assembly keys are diagnostics.

## Runtime and Lowering Notes

Project loading determines compilation units, module paths, output artifacts, and link behavior before language checking.

## Diagnostics

Common project diagnostics include malformed manifest, unknown keys, invalid assembly kind, missing root, overlapping source roots, and ambiguous root ownership.

## Reference Corpus

- `HelloUltraviolet/Source/Reference/Projects/**`
- `Ultraviolet.toml`
- `HelloUltraviolet/Ultraviolet.toml`

## Spec Fallback

Open `references/SPECIFICATION.md` chapter `3` for manifest key tables, source root rules, output paths, and project diagnostics.
