# HelloUltraviolet Conformance Suite

HelloUltraviolet is the reference corpus and conformance suite for validating Ultraviolet language behavior against the canonical repo obligation ledger at `Docs/Internal/UltravioletObligations.csv`.

---

## Layout

- `Source/Reference/`: Contains direct Ultraviolet source exercises for language constructs, runtime behavior, authority, memory ownership, typestates/modal types, and project semantics.
- `Source/Audit/FixtureCatalog/`: Contains compiled fixture indexes and artifact verifiers used by the executable corpus.
- `Source/Audit/Catalog/`: Maps each obligation row in `UltravioletObligations.csv` to the source exercise, fixture, or reference surface that covers it.
- `Source/Audit/SymbolExecutions/`: Groups compiled symbol execution checks by reference or fixture responsibility.
- `Source/Audit/`: Runtime audit checks that prove the generated catalog, fixture references, and compiled symbol surface are correctly wired.
- `Fixtures/`: Physical fixture projects, rejected source files, diagnostics, and expected compiler outputs read by the corpus at runtime.
- `Audit/`: Project-local manifests, non-compliance notes, and clarification ledgers.

The entry point is `Source/Main.uv`. A clean run exits `0`. Failing checks print the failing reference or missing artifact path before returning a non-zero exit code.

---

## Running the Conformance Suite

HelloUltraviolet is the release test surface for the Ultraviolet compiler and runtime. You can build it, run the executable checks, and run source-native unit tests.

### 1. Build the Suite
Build the project using the bootstrap `uv` compiler, specifying the target profile:
```bash
uv build HelloUltraviolet --target-profile x86_64-win64  # Or x86_64-sysv, etc.
```

### 2. Run the Corpus Executable
The built executable is outputted to `Build/Binary/HelloUltraviolet.exe` (or `Build/Binary/HelloUltraviolet` on Linux).

> [!IMPORTANT]
> **Working Directory Restriction**: You must run the compiled executable from the **workspace root directory** (the parent directory of `HelloUltraviolet/`). If run from inside the binary output directory or from within `HelloUltraviolet/`, the relative path fixture and log checks will fail.

```bash
# Run from the workspace root
./Build/Binary/HelloUltraviolet.exe

# Run with full audit validation
./Build/Binary/HelloUltraviolet.exe --audit
```

### 3. Run Source-Native Tests
You can execute source-native unit tests (defined with `#test`) directly using the `uv test` command:
```bash
# Run all tests
uv test HelloUltraviolet --target-profile x86_64-win64

# Run specific tests matching a name pattern
uv test HelloUltraviolet --target-profile x86_64-win64 --test <name>

# Run tests covering a specific obligation anchor
uv test HelloUltraviolet --target-profile x86_64-win64 --coverage <anchor>

# Run tests in a specific file
uv test HelloUltraviolet/Source/Tests/SourceNativeTests.uv --target-profile x86_64-win64
```

---

## Adding Coverage

Standalone Bootstrap compiler or runtime test programs should not be added for behavioral coverage. New behavioral coverage belongs in this corpus as reference source files, fixtures, diagnostics, artifact checks, or source-native `#test` procedures.

* The removed standalone test reconciliation is recorded in `Audit/StandaloneTestReconciliation.md`.
* The obligation exercise audit is recorded in `Audit/ObligationExerciseAudit.md`.
