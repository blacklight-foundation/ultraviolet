# Ultraviolet

Ultraviolet is a general-purpose systems programming language designed for explicit
authority, predictable behavior, and source code that is practical for both
machine generation and human review.

The project includes the language specification, reference compiler, runtime,
tooling, and conformance material for Ultraviolet.

Ultraviolet is in alpha. The specification is the source of truth, and the
reference implementation is evolving toward that specification.

## Why Ultraviolet

Ultraviolet is built around a small set of language-level design rules:

- **One Correct Way:** each semantic operation should have one accepted source form
  unless an alternate form changes meaning, authority, ownership, synchronization,
  ABI behavior, or diagnostics.
- **Local Reasoning:** authority, mutability, movement, synchronization,
  suspension, and dynamic checks should be visible from local syntax and the
  directly referenced type or procedure signature.
- **Explicit over Implicit:** source constructs must not hide observable effects,
  allocation, copying, synchronization, suspension, unsafe behavior, or authority
  acquisition.
- **Static by Default:** static checking is the default; dynamic verification,
  synchronization, dispatch, allocation, copying, and foreign trust boundaries
  require explicit opt-in.

## Project Shape

A minimal executable project has this shape:

```text
Ultraviolet.toml
src/
  Main.uv
```

`Ultraviolet.toml`:

```toml
assembly = { name = "hello", kind = "executable", root = "src" }
```

`src/Main.uv`:

```uv
public procedure main(move ctx: Context) -> i32 {
    return 0
}
```

Ultraviolet source files use the `.uv` extension. Project metadata for user
projects lives in `Ultraviolet.toml`.

## Language Highlights

- **No ambient authority:** external effects require explicit capability values,
  usually introduced through `Context`.
- **Separate responsibility and permission:** `move` transfers responsibility,
  while `const`, `shared`, and `unique` describe access permissions.
- **Scoped memory:** `region` and `frame` make arena allocation and reset behavior
  part of the language surface.
- **Modal types:** state machines, protocols, and typestate can be represented
  directly with state-specific fields, methods, and transitions.
- **Keyed shared access:** `shared` data is synchronized through a language-level
  key system rather than ad hoc locking conventions.
- **Structured parallelism:** `parallel`, `spawn`, and `dispatch` compose with
  explicit CPU, GPU, and inline execution domains.
- **Async as state:** async values are modal state machines with explicit
  suspension, resumption, completion, and failure states.
- **Static verification:** contracts, invariants, and refinement types are checked
  statically by default, with dynamic fallback only where explicitly requested.
- **Compile-time execution:** reflection, quote/splice, and emission support
  generated code without falling back to stringly source rewriting.
- **Explicit foreign boundaries:** FFI rules preserve capability, layout, ABI,
  contract, and unwind behavior across external calls.

## Building From Source

The alpha compiler is the Bootstrap implementation under
`Bootstrap/Ultraviolet`. It builds the `uv` command-line tool, compiler support
tools, runtime support library, and conformance checks with the CMake presets in
`Bootstrap/Ultraviolet/CMakePresets.json`.

From `Bootstrap/Ultraviolet`, build the release package with:

```bash
cmake --preset windows-release
cmake --build --preset windows-release-package
```

On Linux hosts, use the matching `linux-release` and `linux-release-package`
presets.

The package build stages the public compiler entry point as `uv.exe` on Windows
or `uv` on Linux under the preset build output directory.

## Tools

The Ultraviolet toolchain is organized around:

- `uv`: the compiler and project command-line interface
- the runtime library for memory, strings, capabilities, filesystem access,
  structured concurrency, panic handling, and hosted sessions
- conformance and diagnostic tooling for keeping the implementation aligned with
  the specification

## Documentation

- [Language Specification](Docs/SPECIFICATION.md)

The language specification is the source of truth for syntax, static semantics,
dynamic semantics, lowering, diagnostics, ABI behavior, and conformance.

## License

Ultraviolet is licensed under the Apache License, Version 2.0. See
[LICENSE.md](LICENSE.md) for details.
