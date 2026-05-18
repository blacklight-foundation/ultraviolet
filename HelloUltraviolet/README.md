# HelloUltraviolet

HelloUltraviolet is a reference corpus for exercising Ultraviolet language obligations from
the canonical repo obligation ledger at `Docs/Audit/UltravioletObligations.csv`.

## Layout

- `Source/Reference` contains direct Ultraviolet source exercises for language constructs,
  runtime behavior, authority, ownership, typing, lowering, and project semantics.
- `Source/Audit/FixtureCatalog` contains compiled fixture indexes and artifact verifiers used by the
  executable corpus.
- `Source/Audit` contains runtime audit checks that prove the generated catalog, fixture
  references, and compiled symbol surface are wired into the executable.
- `Source/Audit/Catalog` maps each obligation row to the source exercise, fixture, or reference
  surface that covers it.
- `Source/Audit/SymbolExecutions` groups compiled symbol execution checks by reference or fixture
  responsibility.
- `Fixtures` contains physical fixture projects, rejected sources, diagnostics, and expected
  artifacts read by the corpus at runtime.
- `Audit` contains project-local manifests, non-compliance notes, and clarification ledgers.

The executable entry point is `Source/Main.uv`. A clean run exits `0`; failing checks print the
named reference or missing artifact path before returning a non-zero exit code.
