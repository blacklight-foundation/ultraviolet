# HelloUltraviolet Completion Audit

Objective: execute `.agents/plans/HelloUltravioletReferenceCorpus.md`.

## Verified Deliverables

- Project folder exists at `HelloUltraviolet/`.
- Project-local obligation ledger exists at
  `HelloUltraviolet/Audit/UltravioletObligations.csv`.
- The generated ledger check passes:
  `python3 Tools/ExtractObligationLedger.py --check`.
- `HelloUltraviolet/Source/Main.uv` calls the executable reference corpus through
  `runReferenceCorpus(context)`.
- All 149 files under `HelloUltraviolet/Source/Reference` contain executable
  reference bodies; the simple placeholder run-body count is `0`.
- The generated catalog contains 5,986 primary obligation rows in
  `HelloUltraviolet/Source/Audit/Catalog/**/*.uv`. Each row records the
  obligation id, internal spec line, module path, symbol, and source path in
  Ultraviolet source.
- `CoverageCheck.uv` verifies both the generated obligation total and the
  generated primary-reference validation total against `EXPECTED_OBLIGATION_COUNT`.
- `CatalogCsvMembership.uv` compares the 5,986 project-local CSV obligation
  keys with the 5,986 generated catalog primary keys in compiled Ultraviolet
  source.
- `CatalogPrimaryReferences.uv` checks that the 5,986 generated primary
  obligation references are unique by sorting `(id, internal_spec_line)` keys
  and verifying strict adjacent ordering in compiled Ultraviolet source.
- `CatalogSourcePaths.uv` checks the 65 unique source files referenced by
  generated catalog rows, and `HelloUltraviolet.exe` validates their runtime
  existence through `catalogSourcePathsExist(context)`.
- `CatalogSymbols.uv` imports and executes the 65 unique compiled reference
  symbols named by catalog rows, and `HelloUltraviolet.exe` validates them
  through `catalogCompiledSymbolsExecute()`.
- `Source/Fixtures/RejectedSource` compiles metadata for 112 rejected-source
  fixture specimens, and `HelloUltraviolet.exe` validates that fixture index
  through `rejectedSourceFixturesAreIndexed`.
- The 112 rejected-source fixture projects under
  `HelloUltraviolet/Fixtures/RejectedSource` fail with their expected SPEC
  diagnostic code or static-rule diagnostic when built individually with
  `Cursive.exe build ... --check`.
- Each rejected-source fixture includes an `Expected.uv` metadata artifact, and
  the compiled metadata records both the invalid source path and expected
  diagnostic metadata path.
- `HelloUltraviolet.exe` verifies runtime existence of each rejected fixture
  manifest, invalid source file, and `Expected.uv` artifact through
  `rejectedSourceFixtureArtifactsExist(context)`.
- `ExpectedFiles.uv` reads the 112 current rejected-source `Expected.uv`
  artifacts and `HelloUltraviolet.exe` validates exact metadata content through
  one named check per specimen.
- `Source/Fixtures/AcceptedProjects` compiles metadata for 3 accepted project
  fixture specimens, and `HelloUltraviolet.exe` validates both the index and
  artifact paths through accepted-project fixture checks.
- `Fixtures/AcceptedProjects/StaticLibrary` builds as a valid static library
  project, `Fixtures/AcceptedProjects/ExecutableMain` builds and runs as a
  valid executable project, and `Fixtures/AcceptedProjects/PtrNullReturn`
  builds as a valid library project for checked `Ptr::null()` return typing.
- `Source/Fixtures/ArtifactProjects` compiles metadata for 3 artifact project
  fixture specimens, and `HelloUltraviolet.exe` validates both the index and
  source/manifest paths through artifact-project fixture checks.
- `Fixtures/ArtifactProjects/StaticLibraryArchive` builds a `.lib` archive and
  `.obj`, `Fixtures/ArtifactProjects/EmitLlLibrary` builds a `.ll`, `.lib`, and
  `.obj`, and `Fixtures/ArtifactProjects/ExecutableOutput` builds and runs an
  `.exe`, `.map`, and `.obj` artifact.
- The project check gate passes:
  `Cursive.exe build HelloUltraviolet --check --target-profile x86_64-win64
  --incremental off --build-progress off`.
- The project build gate passes:
  `Cursive.exe build HelloUltraviolet --target-profile x86_64-win64
  --incremental off --build-progress off`.
- The executable gate passes:
  `HelloUltraviolet/build/bin/HelloUltraviolet.exe`.
- The focused bootstrap regression gate passes:
  `cursive_codegen_abi_sret_test.exe`.
- Whitespace validation passes:
  `git diff --check -- HelloUltraviolet ...bootstrap files...`.

## Completion Blockers

- Rejected-source fixtures are partially populated. The current fixture set
  covers 112 source, parsing, name-resolution, procedure, statement, expression,
  and pattern diagnostics; the full expected-diagnostics obligation surface is
  not yet represented. Direct expected-diagnostics coverage is `102/382`
  obligation keys.
- Accepted-project fixtures are partially populated with 3 buildable projects.
  Artifact-project fixtures are partially populated with 3 buildable projects.
- Some high-level areas currently use executable reference models rather than
  source specimens for every concrete syntax form, notably polymorphism, keys,
  structured parallelism, async, compile-time forms, FFI, and backend artifacts.

## Current Status

The reference corpus is executable, the generated catalog now materializes every
ledger row in Ultraviolet source, and all verification gates listed above pass.
The plan is not complete until the remaining fixture and concrete-specimen
requirements above are implemented and verified.
